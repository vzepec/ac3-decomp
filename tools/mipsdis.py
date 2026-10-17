#!/usr/bin/env python3
"""mipsdis — desensamblador MIPS R3000 minimo (con COP2) para leer ACE.BIN.

Para cuando la descompilacion de Ghidra corta una funcion (ramas de cola, funciones
que comparten codigo) y hace falta ver las instrucciones reales. Cubre las
instrucciones enteras, saltos, cargas/almacenamientos y COP2, y anota la direccion
efectiva de lui + desplazamiento (el desplazamiento tiene signo: lui 0x800C con
-26648 es 0x800B97E8, no 0x800C97E8; ese error ya se cometio una vez).

Uso:
    python3 tools/mipsdis.py data/exe/ace_eu.bin 0x800AAC08 [num_instrucciones]
"""
import struct
import sys

REG = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']
SPECIAL = {0: 'sll', 2: 'srl', 3: 'sra', 4: 'sllv', 6: 'srlv', 7: 'srav', 8: 'jr', 9: 'jalr', 0x0C: 'syscall',
           0x0D: 'break', 0x10: 'mfhi', 0x11: 'mthi', 0x12: 'mflo', 0x13: 'mtlo', 0x18: 'mult', 0x19: 'multu',
           0x1A: 'div', 0x1B: 'divu', 0x20: 'add', 0x21: 'addu', 0x22: 'sub', 0x23: 'subu', 0x24: 'and',
           0x25: 'or', 0x26: 'xor', 0x27: 'nor', 0x2A: 'slt', 0x2B: 'sltu'}
IMM = {8: 'addi', 9: 'addiu', 0x0A: 'slti', 0x0B: 'sltiu', 0x0C: 'andi', 0x0D: 'ori', 0x0E: 'xori', 0x0F: 'lui'}
MEM = {0x20: 'lb', 0x21: 'lh', 0x22: 'lwl', 0x23: 'lw', 0x24: 'lbu', 0x25: 'lhu', 0x26: 'lwr', 0x28: 'sb',
       0x29: 'sh', 0x2A: 'swl', 0x2B: 'sw', 0x2E: 'swr', 0x32: 'lwc2', 0x3A: 'swc2'}
BR = {4: 'beq', 5: 'bne', 6: 'blez', 7: 'bgtz'}


def dis(w, pc):
    op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    sh, fn, imm = (w >> 6) & 31, w & 63, w & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    if w == 0:
        return 'nop'
    if op == 0:
        n = SPECIAL.get(fn, 'special_%02x' % fn)
        if fn in (0, 2, 3):
            return '%s %s, %s, %d' % (n, REG[rd], REG[rt], sh)
        if fn in (4, 6, 7):
            return '%s %s, %s, %s' % (n, REG[rd], REG[rt], REG[rs])
        if fn == 8:
            return 'jr %s' % REG[rs]
        if fn == 9:
            return 'jalr %s, %s' % (REG[rd], REG[rs])
        if fn in (0x10, 0x12):
            return '%s %s' % (n, REG[rd])
        if fn in (0x11, 0x13):
            return '%s %s' % (n, REG[rs])
        if fn in (0x18, 0x19, 0x1A, 0x1B):
            return '%s %s, %s' % (n, REG[rs], REG[rt])
        if fn in (0x0C, 0x0D):
            return n
        return '%s %s, %s, %s' % (n, REG[rd], REG[rs], REG[rt])
    if op == 1:
        n = {0: 'bltz', 1: 'bgez', 0x10: 'bltzal', 0x11: 'bgezal'}.get(rt, 'regimm')
        return '%s %s, %08x' % (n, REG[rs], (pc + 4 + (simm << 2)) & 0xFFFFFFFF)
    if op in (2, 3):
        return '%s %08x' % ('j' if op == 2 else 'jal', ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    if op in BR:
        if op in (6, 7):
            return '%s %s, %08x' % (BR[op], REG[rs], (pc + 4 + (simm << 2)) & 0xFFFFFFFF)
        return '%s %s, %s, %08x' % (BR[op], REG[rs], REG[rt], (pc + 4 + (simm << 2)) & 0xFFFFFFFF)
    if op in IMM:
        if op == 0x0F:
            return 'lui %s, 0x%04x' % (REG[rt], imm)
        v = imm if op in (0x0C, 0x0D, 0x0E) else simm
        return '%s %s, %s, %s' % (IMM[op], REG[rt], REG[rs], hex(v) if op in (0x0C, 0x0D, 0x0E) else v)
    if op == 0x12:
        if w & 0x02000000:
            return 'cop2 0x%07x' % (w & 0x1FFFFFF)
        n = {0: 'mfc2', 2: 'cfc2', 4: 'mtc2', 6: 'ctc2'}.get(rs, 'cop2?')
        return '%s %s, $%d' % (n, REG[rt], rd)
    if op == 0x10:
        return 'cop0 rs=%d rt=%s rd=%d' % (rs, REG[rt], rd)
    if op in MEM:
        t = ('$%d' % rt) if op in (0x32, 0x3A) else REG[rt]
        return '%s %s, %d(%s)' % (MEM[op], t, simm, REG[rs])
    return 'op_%02x' % op


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    d = open(sys.argv[1], 'rb').read()
    base = 0x80010000
    if d[:8] == b'PS-X EXE':
        base = struct.unpack_from('<I', d, 0x18)[0]
        d = d[2048:]
    a = int(sys.argv[2], 0)
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    hi = {}   # registro -> valor cargado con lui (para anotar direcciones efectivas)
    for k in range(n):
        pc = a + 4 * k
        w = struct.unpack_from('<I', d, pc - base)[0]
        op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        note = ''
        if op in MEM or op == 9:
            if rs in hi:
                note = '    ; -> %08x' % ((hi[rs] + simm) & 0xFFFFFFFF)
        print('%08x  %08x  %s%s' % (pc, w, dis(w, pc), note))
        if op == 0x0F:
            hi[rt] = imm << 16
        elif op in (0x20, 0x21, 0x23, 0x24, 0x25, 9) or op == 0:
            dst = rt if op != 0 else (w >> 11) & 31
            hi.pop(dst, None)


if __name__ == '__main__':
    main()
