/*
  © 2026 Intel Corporation

  This software and the related documents are Intel copyrighted materials, and
  your use of them is governed by the express license under which they were
  provided to you ("License"). Unless the License provides otherwise, you may
  not use, modify, copy, publish, distribute, disclose or transmit this software
  or the related documents without Intel's prior written permission.

  This software and the related documents are provided as is, with no express or
  implied warranties, other than those that are expressly stated in the License.
*/

#include <simics/util/strbuf.h>
#include "cosim.h"
#include "rv32-core.h"
#include "rv32-helpers.h"

static int
alu_r(uint32 funct3, uint32 funct7, uint32 a, uint32 b, uint32 *result)
{
        uint32 shamt = b & 0x1fu;

        switch ((funct7 << 3) | funct3) {
                case 0x000u: *result = a + b; return 1;                         // add
                case 0x100u: *result = a - b; return 1;                         // sub
                case 0x001u: *result = a << shamt; return 1;                    // sll
                case 0x002u: *result = ((int32_t)a < (int32_t)b); return 1;     // slt
                case 0x003u: *result = (a < b); return 1;                       // sltu
                case 0x004u: *result = a ^ b; return 1;                         // xor
                case 0x005u: *result = a >> shamt; return 1;                    // srl
                case 0x105u: *result = (uint32)((int32_t)a >> shamt); return 1; // sra
                case 0x006u: *result = a | b; return 1;                         // or
                case 0x007u: *result = a & b; return 1;                         // and
                default: return 0;
        }
}

static int 
branch_r(uint32 funct3, uint32 a, uint32 b, int *branch_taken)
{
        switch (funct3) {
                case 0x0u: *branch_taken = a == b; return 1; // beq
                case 0x1u: *branch_taken = a != b; return 1; // bne
                case 0x4u: *branch_taken = (int32_t)a < (int32_t)b; return 1; // blt
                case 0x5u: *branch_taken = (int32_t)a >= (int32_t)b; return 1; // bge
                case 0x6u: *branch_taken = a < b; return 1; // bltu
                case 0x7u: *branch_taken = a >= b; return 1; // bgeu
                default: return 0;
        }
}

static uint32
imm_i(uint32 instr)
{
        uint32 imm = instr >> 20;

        if (imm & 0x800u)
                imm |= 0xfffff000u;

        return imm;
}
static uint32
imm_u(uint32 instr)
{
        return instr & 0xfffff000u;
}
static uint32
imm_b(uint32 instr)
{
        uint32 imm = ((instr >> 7) & 0x1eu) | ((instr >> 20) & 0x7e0u)
                        | ((instr << 4) & 0x800u) | ((instr >> 19) & 0x1000u);

        if (imm & 0x1000u)
                imm |= 0xffffe000u;

        return imm;
}
static uint32
imm_j(uint32 instr)
{
        uint32 imm = ((instr >> 20) & 0x7feu) | ((instr >> 9) & 0x800u)
                        | (instr & 0xff000u) | ((instr >> 11) & 0x100000u);

        if (imm & 0x100000u)
                imm |= 0xffe00000u;

        return imm;
}

static decoded_instruction_t
decode(uint32 instr)
{
        decoded_instruction_t decoded;

        decoded.opcode = instr & 0x7fu;
        decoded.rd = (instr >> 7) & 0x1fu;
        decoded.funct3 = (instr >> 12) & 0x7u;
        decoded.rs1 = (instr >> 15) & 0x1fu;
        decoded.rs2 = (instr >> 20) & 0x1fu;
        decoded.funct7 = (instr >> 25) & 0x7fu;
        decoded.imm = 0u;

        switch (decoded.opcode) {
                case 0x13u:
                case 0x67u:
                case 0x73u:
                        decoded.imm = imm_i(instr);
                        break;
                case 0x37u:
                case 0x17u:
                        decoded.imm = imm_u(instr);
                        break;
                case 0x63u:
                        decoded.imm = imm_b(instr);
                        break;
                case 0x6fu:
                        decoded.imm = imm_j(instr);
                        break;
                default:
                        break;
        }

        return decoded;
}

static uint32
forwarding(rv32_t *cpu, uint32 rs_val, unsigned rs)
{
        if (cpu->ex_wb.valid && cpu->ex_wb.write_rd && cpu->ex_wb.decoded.rd == rs && cpu->ex_wb.decoded.rd != 0) {
                rs_val = cpu->ex_wb.result;
        }
        return rs_val;
}

static int
stage_wb(rv32_t *cpu)
{
        if (!cpu->ex_wb.valid) {
                return 0;
        }
        if (cpu->ex_wb.illegal_instr) {
                cpu->pc = cpu->ex_wb.pc;
                illegal_instruction(cpu, cpu->ex_wb.pc, cpu->ex_wb.instr);
                increment_cycle(cpu);
                return 1;
        }
        if (cpu->ex_wb.ebreak) {
                cpu->pc = cpu->ex_wb.pc;
                increment_step(cpu);
                increment_cycle(cpu);
                ebreak(cpu);
                return 1;
        }
        // Write the result to the destination register if needed
        if (cpu->ex_wb.write_rd) {
                reg_write_x(cpu, cpu->ex_wb.decoded.rd, cpu->ex_wb.result);
        }
        // Update the program counter to the next instruction
        cpu->pc = cpu->ex_wb.next_pc;
        increment_step(cpu);
        return 0;
}

static int
stage_ex(rv32_t *cpu, uint32 *target)
{
        int redirect = 0;
        ex_wb_reg_t next = {0};

        if (cpu->id_ex.valid) {
                next.valid = 1;
                next.pc = cpu->id_ex.pc;
                next.next_pc = next.pc + 4u;
                next.decoded = cpu->id_ex.decoded;
                next.instr = cpu->id_ex.instr;
                next.ebreak = cpu->id_ex.ebreak;

                if (!next.ebreak) {
                        uint32 a = forwarding(cpu, cpu->id_ex.rs1_val, cpu->id_ex.decoded.rs1);
                        uint32 b = forwarding(cpu, cpu->id_ex.rs2_val, cpu->id_ex.decoded.rs2);

                        // switch on the opcode to determine the instruction type and execute it
                        switch (next.decoded.opcode) {
                                case 0x13u: { // OP_IMM
                                        int is_shift = (next.decoded.funct3 == 1u || next.decoded.funct3 == 5u);
                                        uint32 operand;

                                        // Shift only use imm[4:0]
                                        operand = next.decoded.imm;

                                        if (!alu_r(next.decoded.funct3, is_shift ? next.decoded.funct7 : 0u,
                                                        a,
                                                        operand, &next.result)) {
                                                next.illegal_instr = 1;
                                                break;
                                        }
                                        next.write_rd = 1;

                                        break;
                                }
                                case 0x67u: { // JALR
                                        next.result = next.next_pc;
                                        next.next_pc = (a + next.decoded.imm) & ~1u;
                                        next.write_rd = 1;
                                        break;
                                }
                                case 0x33u: { // OP
                                        if (!alu_r(next.decoded.funct3, next.decoded.funct7,
                                                        a,
                                                        b,
                                                        &next.result)) {
                                                next.illegal_instr = 1;
                                                break;
                                        }

                                        next.write_rd = 1;
                                        break;
                                }
                                case 0x37u: { // LUI
                                        next.result = next.decoded.imm;
                                        next.write_rd = 1;
                                        break;
                                }
                                case 0x17u: { // AUIPC
                                        next.result = next.pc + next.decoded.imm;
                                        next.write_rd = 1;
                                        break;
                                }
                                case 0x63u: { // BRANCH
                                        int branch_taken = 0;
                                        if (!branch_r(next.decoded.funct3, a, b, &branch_taken)) {
                                                next.illegal_instr = 1;
                                                break;
                                        }
                                        if (branch_taken) {
                                                next.next_pc = next.pc + next.decoded.imm;
                                        }
                                        break;
                                }
                                case 0x6fu: { // JAL
                                        next.result = next.next_pc;
                                        next.next_pc = next.pc + next.decoded.imm;
                                        next.write_rd = 1;
                                        break;
                                }

                                default:
                                        next.illegal_instr = 1;
                                        break;
                        }
                }
        }
        if (next.valid && !next.illegal_instr && !next.ebreak && next.next_pc != next.pc + 4) {
                *target = next.next_pc;
                redirect = 1;
        }
        cpu->ex_wb = next;
        return redirect;
}

static void
stage_id(rv32_t *cpu, int redirect)
{
        id_ex_reg_t next = {0};
        if (cpu->if_id.valid && !redirect) {
                if (cpu->if_id.instr == 0x00100073u) { // EBREAK
                        next.valid = 1;
                        next.pc = cpu->if_id.pc;
                        next.instr = cpu->if_id.instr;
                        next.ebreak = 1;
                } else {
                        decoded_instruction_t decoded = decode(cpu->if_id.instr);
                        // Read the source registers
                        next.rs1_val = reg_read_x(cpu, decoded.rs1);
                        next.rs2_val = reg_read_x(cpu, decoded.rs2);
                        next.valid = 1;
                        next.pc = cpu->if_id.pc;
                        next.instr = cpu->if_id.instr;
                        next.decoded = decoded;
                }
        }
        cpu->id_ex = next;
}

static void
stage_if(rv32_t *cpu, int redirect, uint32 target)
{
        if (redirect) {
                cpu->fetch_pc = target;
        } else {
                uint8 bytes[4];
                uint32 pc = cpu->fetch_pc;

                // Search the instruction in memory
                if (read_simics_memory(cpu, pc, bytes, sizeof bytes)) {
                        cpu->if_id.instr = (uint32)bytes[0]
                                | ((uint32)bytes[1] << 8)
                                | ((uint32)bytes[2] << 16)
                                | ((uint32)bytes[3] << 24);
                        cpu->if_id.valid = 1;
                        cpu->if_id.pc = pc;
                        cpu->fetch_pc += 4u;
                        return;
                }
        }
        cpu->if_id.valid = 0;
}

void
execute(rv32_t *cpu)
{
        uint32 target = 0;
        int wb_halted;
        wb_halted = stage_wb(cpu);
        if (wb_halted) {
                return;
        }
        int redirect = stage_ex(cpu, &target);
        stage_id(cpu, redirect);
        stage_if(cpu, redirect, target);
        increment_cycle(cpu);
}

/* Text for Simics' disassemble CLI command (e.g. "disassemble 0x0 10")
   for the instruction word instr. Return a string allocated with Simics'
   allocators, e.g. MM_STRDUP() or a strbuf as below; Simics frees it. */
char *
rv32_disassemble(uint32 instr)
{
        strbuf_t b = SB_INIT;
        sb_addfmt(&b, ".word 0x%08x", instr);
        return sb_detach(&b);
}
