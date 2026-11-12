# © 2026 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and
# your use of them is governed by the express license under which they were
# provided to you ("License"). Unless the License provides otherwise, you may
# not use, modify, copy, publish, distribute, disclose or transmit this software
# or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or
# implied warranties, other than those that are expressly stated in the License.

# Traductor de ensamblador RV32I (subconjunto del curso) a código máquina.
#
# Convenciones de los programas (clase del 26 oct 2026):
#   - Una instrucción por línea; las líneas vacías se ignoran.
#   - '#' inicia un comentario hasta el final de la línea.
#   - Operandos separados por comas; los espacios extra se ignoran.
#   - Registros x0..x31.
#   - Números decimales o hexadecimales (0x...), con signo opcional.
#   - El destino de un branch o jal es un offset en bytes desde esa instrucción.
#   - Sin etiquetas ni pseudoinstrucciones. ebreak termina el programa.
#
# Fuera de Simics se puede usar para ver el listado de un programa:
#   python txt_loader.py programa.txt

import re
import sys

try:
    import simics
except ImportError:      # fuera de Simics: solo se usa el listado
    simics = None


def mem_write8(core, addr, value):
    """Write the byte value (0..255) to address addr of the core's memory."""
    simics.SIM_write_phys_memory(core, addr, value, 1)


# ---------------------------------------------------------------------------
# Tablas de codificación (RISC-V ISA Manual Vol. I, cap. 2)
# ---------------------------------------------------------------------------

OP_LUI    = 0b0110111
OP_AUIPC  = 0b0010111
OP_JAL    = 0b1101111
OP_JALR   = 0b1100111
OP_BRANCH = 0b1100011
OP_IMM    = 0b0010011
OP_REG    = 0b0110011
OP_SYSTEM = 0b1110011

# Tipo R: mnemónico -> (funct3, funct7)
R_TYPE = {
    "add":  (0b000, 0b0000000),
    "sub":  (0b000, 0b0100000),
    "sll":  (0b001, 0b0000000),
    "slt":  (0b010, 0b0000000),
    "sltu": (0b011, 0b0000000),
    "xor":  (0b100, 0b0000000),
    "srl":  (0b101, 0b0000000),
    "sra":  (0b101, 0b0100000),
    "or":   (0b110, 0b0000000),
    "and":  (0b111, 0b0000000),
}

# Tipo I aritmético/lógico con inmediato de 12 bits: mnemónico -> funct3
I_TYPE = {
    "addi":  0b000,
    "slti":  0b010,
    "sltiu": 0b011,
    "xori":  0b100,
    "ori":   0b110,
    "andi":  0b111,
}

# Shifts con inmediato (tipo I, shamt de 5 bits): mnemónico -> (funct3, funct7)
SHIFT_TYPE = {
    "slli": (0b001, 0b0000000),
    "srli": (0b101, 0b0000000),
    "srai": (0b101, 0b0100000),
}

# Tipo B: mnemónico -> funct3
B_TYPE = {
    "beq":  0b000,
    "bne":  0b001,
    "blt":  0b100,
    "bge":  0b101,
    "bltu": 0b110,
    "bgeu": 0b111,
}

# Tipo U: mnemónico -> opcode
U_TYPE = {
    "lui":   OP_LUI,
    "auipc": OP_AUIPC,
}


class AsmError(Exception):
    """Error de ensamblado; el mensaje no incluye la línea (se agrega luego)."""


# ---------------------------------------------------------------------------
# Lectura de operandos
# ---------------------------------------------------------------------------

def parse_reg(text):
    """'x5' -> 5. Solo se aceptan x0..x31."""
    m = re.fullmatch(r"x(\d+)", text.strip().lower())
    if not m or int(m.group(1)) > 31:
        raise AsmError("registro inválido '%s' (se esperaba x0..x31)" % text)
    return int(m.group(1))


def parse_num(text):
    """'20', '-8', '+16', '0xff', '-0x10' -> entero de Python."""
    m = re.fullmatch(r"([+-]?)(0[xX][0-9a-fA-F]+|\d+)", text.strip())
    if not m:
        raise AsmError("número inválido '%s'" % text)
    sign, digits = m.groups()
    value = int(digits, 16) if digits.lower().startswith("0x") else int(digits, 10)
    return -value if sign == "-" else value


def check_range(value, lo, hi, what):
    if not lo <= value <= hi:
        raise AsmError("%s %d fuera de rango [%d, %d]" % (what, value, lo, hi))


def check_even(value, what):
    if value % 2 != 0:
        raise AsmError("%s %d debe ser par" % (what, value))


def expect_operands(mnemonic, ops, n, form):
    if len(ops) != n:
        raise AsmError("'%s' espera %d operandos: %s %s"
                       % (mnemonic, n, mnemonic, form))


# ---------------------------------------------------------------------------
# Codificadores por formato. Cada uno devuelve la palabra de 32 bits.
# Los inmediatos negativos se pasan a complemento a dos con '& mask'.
# ---------------------------------------------------------------------------

def enc_r(funct7, rs2, rs1, funct3, rd, opcode):
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) \
        | (rd << 7) | opcode


def enc_i(imm, rs1, funct3, rd, opcode):
    return ((imm & 0xFFF) << 20) | (rs1 << 15) | (funct3 << 12) \
        | (rd << 7) | opcode


def enc_b(imm, rs2, rs1, funct3, opcode):
    imm &= 0x1FFF                         # 13 bits: imm[12:0], imm[0] = 0
    bit12   = (imm >> 12) & 0x1
    bit11   = (imm >> 11) & 0x1
    b10_5   = (imm >> 5) & 0x3F
    b4_1    = (imm >> 1) & 0xF
    return (bit12 << 31) | (b10_5 << 25) | (rs2 << 20) | (rs1 << 15) \
        | (funct3 << 12) | (b4_1 << 8) | (bit11 << 7) | opcode


def enc_u(imm20, rd, opcode):
    return ((imm20 & 0xFFFFF) << 12) | (rd << 7) | opcode


def enc_j(imm, rd, opcode):
    imm &= 0x1FFFFF                       # 21 bits: imm[20:0], imm[0] = 0
    bit20    = (imm >> 20) & 0x1
    b10_1    = (imm >> 1) & 0x3FF
    bit11    = (imm >> 11) & 0x1
    b19_12   = (imm >> 12) & 0xFF
    return (bit20 << 31) | (b10_1 << 21) | (bit11 << 20) | (b19_12 << 12) \
        | (rd << 7) | opcode


# ---------------------------------------------------------------------------
# Ensamblado de una instrucción
# ---------------------------------------------------------------------------

def assemble_line(mnemonic, ops):
    """Traduce una instrucción ya separada en mnemónico y operandos."""
    if mnemonic in R_TYPE:
        expect_operands(mnemonic, ops, 3, "rd, rs1, rs2")
        funct3, funct7 = R_TYPE[mnemonic]
        rd, rs1, rs2 = (parse_reg(o) for o in ops)
        return enc_r(funct7, rs2, rs1, funct3, rd, OP_REG)

    if mnemonic in I_TYPE:
        expect_operands(mnemonic, ops, 3, "rd, rs1, imm")
        rd, rs1 = parse_reg(ops[0]), parse_reg(ops[1])
        imm = parse_num(ops[2])
        check_range(imm, -2048, 2047, "inmediato")
        return enc_i(imm, rs1, I_TYPE[mnemonic], rd, OP_IMM)

    if mnemonic in SHIFT_TYPE:
        expect_operands(mnemonic, ops, 3, "rd, rs1, shamt")
        funct3, funct7 = SHIFT_TYPE[mnemonic]
        rd, rs1 = parse_reg(ops[0]), parse_reg(ops[1])
        shamt = parse_num(ops[2])
        check_range(shamt, 0, 31, "shamt")
        # imm[11:5] = funct7, imm[4:0] = shamt
        return enc_i((funct7 << 5) | shamt, rs1, funct3, rd, OP_IMM)

    if mnemonic in B_TYPE:
        expect_operands(mnemonic, ops, 3, "rs1, rs2, offset")
        rs1, rs2 = parse_reg(ops[0]), parse_reg(ops[1])
        off = parse_num(ops[2])
        check_range(off, -4096, 4094, "offset")
        check_even(off, "offset")
        return enc_b(off, rs2, rs1, B_TYPE[mnemonic], OP_BRANCH)

    if mnemonic in U_TYPE:
        expect_operands(mnemonic, ops, 2, "rd, imm")
        rd = parse_reg(ops[0])
        imm = parse_num(ops[1])
        # 20 bits superiores; se acepta 0..0xFFFFF o un valor negativo de 20 bits
        check_range(imm, -(1 << 19), (1 << 20) - 1, "inmediato")
        return enc_u(imm, rd, U_TYPE[mnemonic])

    if mnemonic == "jal":
        expect_operands(mnemonic, ops, 2, "rd, offset")
        rd = parse_reg(ops[0])
        off = parse_num(ops[1])
        check_range(off, -(1 << 20), (1 << 20) - 2, "offset")
        check_even(off, "offset")
        return enc_j(off, rd, OP_JAL)

    if mnemonic == "jalr":
        expect_operands(mnemonic, ops, 2, "rd, offset(rs1)")
        rd = parse_reg(ops[0])
        m = re.fullmatch(r"(.+?)\s*\(\s*(.+?)\s*\)", ops[1])
        if not m:
            raise AsmError("se esperaba offset(rs1), no '%s'" % ops[1])
        off = parse_num(m.group(1))
        rs1 = parse_reg(m.group(2))
        check_range(off, -2048, 2047, "offset")
        return enc_i(off, rs1, 0b000, rd, OP_JALR)

    if mnemonic == "ebreak":
        expect_operands(mnemonic, ops, 0, "")
        return enc_i(1, 0, 0b000, 0, OP_SYSTEM)

    raise AsmError("instrucción desconocida '%s'" % mnemonic)


def assemble_file(path):
    """Lee el archivo y devuelve una lista de (palabra, nro_línea, texto)."""
    program = []
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, start=1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split(None, 1)
            mnemonic = parts[0].lower()
            ops = []
            if len(parts) > 1:
                ops = [o.strip() for o in parts[1].split(",")]
                if any(o == "" for o in ops):
                    raise AsmError("%s:%d: operando vacío en '%s'"
                                   % (path, lineno, line))
            try:
                word = assemble_line(mnemonic, ops)
            except AsmError as e:
                raise AsmError("%s:%d: %s" % (path, lineno, e)) from None
            program.append((word, lineno, line))
    return program


def load_program(core, path, address):
    """Read the assembly program in the text file path and write the machine
    code of its instructions into memory, the first one at address."""
    try:
        program = assemble_file(path)
    except AsmError as e:
        raise simics.SimExc_General(str(e)) from None

    addr = address
    for word, _, _ in program:
        for i in range(4):                       # little-endian
            mem_write8(core, addr + i, (word >> (8 * i)) & 0xFF)
        addr += 4


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("uso: python txt_loader.py programa.txt")
    try:
        prog = assemble_file(sys.argv[1])
    except AsmError as e:
        sys.exit(str(e))
    for n, (word, lineno, text) in enumerate(prog):
        print("0x%02x  0x%08X   %s" % (4 * n, word, text))
