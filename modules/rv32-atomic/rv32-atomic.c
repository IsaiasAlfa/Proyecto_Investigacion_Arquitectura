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

        uint32 opcode = instr & 0x7fu;
        uint32 funct3 = (instr >> 12) & 0x7u;

        // ADDI
        if (opcode == 0x13u && funct3 == 0u) {
                unsigned rd = (instr >> 7) & 0x1fu;
                unsigned rs1 = (instr >> 15) & 0x1fu;
                uint32 imm = instr >> 20;

                if (imm & 0x800u)
                        imm |= 0xfffff000u;

                uint32 result = reg_read_x(cpu, rs1) + imm;
                reg_write_x(cpu, rd, result);
                cpu->pc = pc + 4u;

                increment_cycle(cpu);
                increment_step(cpu);
                return;
        }

        if (opcode == 0x33u) {
                unsigned rd = (instr >> 7) & 0x1fu;
                unsigned rs1 = (instr >> 15) & 0x1fu;
                unsigned rs2 = (instr >> 20) & 0x1fu;
                uint32 funct7 = instr >>25;
                uint32 result;

                if (!alu_r(funct3, funct7,
                            reg_read_x(cpu, rs1),
                            reg_read_x(cpu, rs2),
                            &result)) {
                        illegal_instruction(cpu, pc, instr);
                        return;
                }

                reg_write_x(cpu, rd, result);
                cpu->pc = pc + 4u;

                increment_cycle(cpu);
                increment_step(cpu);
                return;
        }

        /*TODO: Demas instrucciones*/
        illegal_instruction(cpu, pc, instr);
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
