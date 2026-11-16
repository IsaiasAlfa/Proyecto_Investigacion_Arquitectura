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


typedef struct decoded_instruction {
        uint32 opcode;
        unsigned rd;
        uint32 funct3;
        unsigned rs1;
        unsigned rs2;
        uint32 funct7;
        uint32 imm;
} decoded_instruction_t;

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

void
execute(rv32_t *cpu)
{
        uint8 bytes[4];
        uint32 pc = cpu->pc;

        // Search the instruction in memory
        if (!read_simics_memory(cpu, pc, bytes, sizeof bytes)) {
                rv32_halt(cpu, "instruction fetch failed");
                return;
        }

        // Re-build the instruction in little-endian format
        uint32 instr = (uint32)bytes[0]
                        | ((uint32)bytes[1] << 8)
                        | ((uint32)bytes[2] << 16)
                        | ((uint32)bytes[3] << 24);

        // EBREAK
        if (instr == 0x00100073u) {
                increment_cycle(cpu);
                increment_step(cpu);
                ebreak(cpu);
                return;
        }

        // Decode the instruction
        decoded_instruction_t decoded = decode(instr);

        // Read the source registers
        uint32 rs1_val = reg_read_x(cpu, decoded.rs1);
        uint32 rs2_val = reg_read_x(cpu, decoded.rs2);

        // Prepare for the result and next PC
        uint32 result = 0u;
        int write_rd = 0;
        uint32 next_pc = pc + 4u;

        // switch on the opcode to determine the instruction type and execute it
        switch (decoded.opcode) {
                case 0x13u: { // OP_IMM
                        int is_shift = (decoded.funct3 == 1u || decoded.funct3 == 5u);
                        uint32 operand;

                        // Shift only use imm[4:0]
                        operand = decoded.imm;

                        if (!alu_r(decoded.funct3, is_shift ? decoded.funct7 : 0u,
                                        rs1_val,
                                        operand, &result)) {
                                illegal_instruction(cpu, pc, instr);
                                return;
                        }
                        write_rd = 1;

                        break;
                }
                case 0x67u: { // JALR
                        result = next_pc;
                        next_pc = (rs1_val + decoded.imm) & ~1u;
                        write_rd = 1;
                        break;
                }
                case 0x33u: { // OP
                        if (!alu_r(decoded.funct3, decoded.funct7,
                                        rs1_val,
                                        rs2_val,
                                        &result)) {
                                illegal_instruction(cpu, pc, instr);
                                return;
                        }

                        write_rd = 1;
                        break;
                }
                case 0x37u: { // LUI
                        result = decoded.imm;
                        write_rd = 1;
                        break;
                }
                case 0x17u: { // AUIPC
                        result = pc + decoded.imm;
                        write_rd = 1;
                        break;
                }
                case 0x63u: { // BRANCH
                        int branch_taken = 0;
                        if (!branch_r(decoded.funct3, rs1_val, rs2_val, &branch_taken)) {
                                illegal_instruction(cpu, pc, instr);
                                return;
                        }
                        if (branch_taken) {
                                next_pc = pc + decoded.imm;
                        }
                        break;
                }
                case 0x6fu: { // JAL
                        result = next_pc;
                        next_pc = pc + decoded.imm;
                        write_rd = 1;
                        break;
                }

                default:
                        illegal_instruction(cpu, pc, instr);
                        return;
        }

        // Write the result to the destination register if needed
        if (write_rd) {
                reg_write_x(cpu, decoded.rd, result);
        }

        // Update the program counter to the next instruction
        cpu->pc = next_pc;
        increment_cycle(cpu);
        increment_step(cpu);
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
