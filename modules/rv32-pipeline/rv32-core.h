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

#ifndef RV32_CORE_H
#define RV32_CORE_H

#include "cosim-core.h"

#define RV32_NUM_REGS   32
typedef struct decoded_instruction {
        uint32 opcode;
        unsigned rd;
        uint32 funct3;
        unsigned rs1;
        unsigned rs2;
        uint32 funct7;
        uint32 imm;
} decoded_instruction_t;
typedef struct if_id_reg {
        int valid;
        uint32 pc;
        uint32 instr;
} if_id_reg_t;

typedef struct id_ex_reg {
        int valid;
        uint32 pc;
        uint32 instr;
        decoded_instruction_t decoded;
        uint32 rs1_val;
        uint32 rs2_val;
        int ebreak;
} id_ex_reg_t;

typedef struct ex_wb_reg {
        int valid;
        uint32 pc;
        uint32 next_pc;
        uint32 instr;
        decoded_instruction_t decoded;
        uint32 result;
        int write_rd;
        int ebreak;
        int illegal_instr;
} ex_wb_reg_t;
typedef struct rv32 {
        cosim_core_t *core;  /* pass to cosim_core_* functions */
        conf_object_t *obj;  /* the core object, for SIM_LOG_* */

        uint32 pc;
        uint32 fetch_pc;
        uint32 x[RV32_NUM_REGS];   /* use reg_read_x/reg_write_x: x0 is zero */
        if_id_reg_t if_id;
        id_ex_reg_t id_ex;
        ex_wb_reg_t ex_wb;
} rv32_t;

bool read_simics_memory(rv32_t *cpu, uint32 addr, uint8 *buf, unsigned len);

void rv32_halt(rv32_t *cpu, const char *reason);

void execute(rv32_t *cpu);

char *rv32_disassemble(uint32 instr);

void rv32_init_class(cosim_core_class_t *core_class);
struct rv32 *rv32_new_instance(cosim_core_t *core);
void rv32_delete_instance(rv32_t *cpu);
rv32_t *rv32_from_core(cosim_core_t *core);
logical_address_t rv32_get_pc(cosim_core_t *core);
void rv32_set_pc(cosim_core_t *core, logical_address_t pc);
void rv32_execute(cosim_core_t *core);

#endif
