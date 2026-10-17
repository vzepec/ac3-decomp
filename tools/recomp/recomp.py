#!/usr/bin/env python3
"""recomp — recompilador estatico MIPS R3000 -> C para el codigo de Ace Combat 3 (etapa 6).

Traduce cada funcion PROPIA del juego (las de Ghidra en el rango del residente o de un
overlay, mas las entradas que se descubren: destinos de jal, direcciones de codigo
cargadas con lui/addiu, etiquetas LAB_ de la descompilacion, destinos de saltos que salen
de una funcion (finales compartidos) y punteros a codigo en los datos) a una funcion de C:
    void f_8002ac0c(RCpu *c);
sobre el entorno de psx/recomp/recomp_rt.h (registros en c->r[], memoria con
direcciones PS1, GTE de psx/). Reglas de traduccion (las mismas que tools/mipsemu.py):
  - hueco de retardo: la condicion del salto (y el registro de jr/jalr) se lee ANTES de la
    instruccion del hueco; jal/jalr escriben $ra antes del hueco;
  - saltos dentro de la funcion -> goto; fuera -> llamada + return (salto de cola); la
    caida al final de la funcion -> llamada a la siguiente;
  - jr $ra -> return, comprobando que $ra es la direccion de retorno esperada (la fija quien
    llama: jal = pc+8; un salto de cola hereda la de la funcion actual); si no, RC_BADRA;
    jr de otro registro -> switch sobre la tabla de saltos detectada
    (lui/addiu + addu + lw) o sobre todas las etiquetas; si no casa, rc_call (cola);
  - jal -> llamada directa si es una funcion traducida, rc_libcall si es de libreria,
    rc_call (despachador) en otro caso; jalr -> rc_call;
  - break -> rc_break (en las pruebas, fallo como en el interprete);
  - presupuesto de instrucciones en los saltos hacia atras y al entrar en cada funcion
    (solo afecta a las pruebas: corta bucles y recursiones infinitas).
El C generado DERIVA del codigo de Namco: se escribe en data/recomp/ y no entra en git.

Overlays (todos se cargan en 0x800C9DC0): cada uno se traduce con su prefijo (fN_) y su
tabla; las llamadas al residente van directas a sus funciones traducidas y el despachador
(rc_call) busca en el residente y en el overlay cargado (rc_set_overlay).

Uso:
    python3 tools/recomp/recomp.py data/exe/ace_eu.bin data/ghidra/all_ace_eu.c data/recomp \
        [--from data/exe/ovl/overlay1.exe,...]
    python3 tools/recomp/recomp.py data/exe/ovl/overlay1.exe data/ghidra/all_overlay1.c data/recomp \
        --overlay 1 --resident data/recomp/ace_eu.json
"""
import json
import os
import re
import struct
import sys

BASE = 0x80010000
# codigo propio del residente: el bloque principal y ulz_decompress (0x800AE118-0x800AE524),
# que el enlazador puso tras las librerias; lo que sigue hasta 0x800C9DC0 son datos
OWN = [(0x800252CC, 0x80099DE0), (0x800AE118, 0x800AE528)]
LIB = (0x80099DE0, 0x800AE098 + 4)
EXTRA_LIB = {0x80025214, 0x80025240, 0x800252BC}   # __main, stup1, stup0 (firmas de SN)
REG = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
       's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra']


class Image:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.base = BASE
        if d[:8] == b'PS-X EXE':
            self.base = struct.unpack_from('<I', d, 0x18)[0]
            d = d[2048:]
        self.d = d

    def word(self, a):
        o = a - self.base
        if o < 0 or o + 4 > len(self.d):
            return None
        return struct.unpack_from('<I', self.d, o)[0]

    def own(self, a):
        return any(lo <= a < hi for lo, hi in OWN)


def is_lib(a):
    return LIB[0] <= a < LIB[1] or a in EXTRA_LIB


def branch_info(w, pc):
    """(tipo, destino) de un salto: tipo en b (condicional), bal, j, jal, jr, jalr; o None"""
    op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
    imm = w & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    tgt = (pc + 4 + (simm << 2)) & 0xFFFFFFFF
    if op == 1:
        return ('bal' if rt & 0x10 else 'b', tgt)
    if op in (4, 5, 6, 7):
        return ('b', tgt)
    if op == 2:
        return ('j', ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    if op == 3:
        return ('jal', ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2))
    if op == 0 and (w & 63) == 8:
        return ('jr', rs)
    if op == 0 and (w & 63) == 9:
        return ('jalr', rs)
    return None


def find_entries(img, ghidra_c):
    names = {}
    for n, a in re.findall(r'// ==== (\S+) @ (\S+)\n', open(ghidra_c, encoding='latin1').read()):
        names[int(a, 16)] = n
    entries = {a for a in names if img.own(a)}
    src = open(ghidra_c, encoding='latin1').read()
    for a in re.findall(r'\bLAB_([0-9a-f]{8})\b', src):   # etiquetas usadas como punteros a codigo
        a = int(a, 16)
        if img.own(a):
            entries.add(a)
            LABS.add(a)
    return sorted(entries), names


def analyze(img, start, limit):
    """bloques alcanzables desde start sin pasar de limit; devuelve (pcs, etiquetas, tablas)"""
    seen, labels, tables, work = set(), {start}, {}, [start]
    while work:
        pc = work.pop()
        while start <= pc < limit and pc not in seen:
            w = img.word(pc)
            if w is None:
                break
            seen.add(pc)
            b = branch_info(w, pc)
            if not b:
                pc += 4
                continue
            seen.add(pc + 4)   # hueco de retardo
            kind, tgt = b
            if kind in ('b', 'bal'):
                if start <= tgt < limit:
                    labels.add(tgt)
                    work.append(tgt)
                labels.add(pc + 8)
                pc += 8
                continue
            if kind in ('jal', 'jalr'):
                pc += 8
                continue
            if kind == 'j':
                if start <= tgt < limit:
                    labels.add(tgt)
                    work.append(tgt)
                break
            if kind == 'jr':
                if tgt == 31:
                    break
                t = jump_table(img, start, pc, limit)
                tables[pc] = t
                for x in t:
                    if start <= x < limit:
                        labels.add(x)
                        work.append(x)
                break
    return seen, labels, tables


def jump_table(img, start, pc, limit):
    """tabla de saltos de un jr: propagacion de constantes (lui, addiu, ori, addu, sll) desde
    unas instrucciones antes; devuelve los destinos validos leidos de la tabla"""
    known, based = {}, {}
    a = max(start, pc - 4 * 24)
    reg = (img.word(pc) >> 21) & 31
    table = None
    count = None
    while a < pc:
        w = img.word(a)
        op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 0x0F:
            known[rt] = imm << 16
            based.pop(rt, None)
        elif op == 0x09 and rs in known:
            known[rt] = (known[rs] + simm) & 0xFFFFFFFF
            based.pop(rt, None)
        elif op == 0x0D and rs in known:
            known[rt] = known[rs] | imm
        elif op == 0x0B:   # sltiu: numero de casos del switch
            count = imm
            known.pop(rt, None)
            based.pop(rt, None)
        elif op == 0 and (w & 63) in (0x20, 0x21):   # add/addu
            if rs in known and rt not in known:
                based[rd] = known[rs]
            elif rt in known and rs not in known:
                based[rd] = known[rt]
            known.pop(rd, None)
        elif op == 0x23:
            if rs in based and rt == reg:
                table = (based[rs] + simm) & 0xFFFFFFFF
            known.pop(rt, None)
            based.pop(rt, None)
        else:
            dst = rd if op == 0 else rt
            if op not in (0x28, 0x29, 0x2B, 1, 4, 5, 6, 7):
                known.pop(dst, None)
                based.pop(dst, None)
        a += 4
    out = []
    if table is not None:
        for k in range(count if count and count <= 1024 else 512):
            t = img.word(table + 4 * k)
            # con el numero de casos se lee la tabla entera (los destinos pueden caer en un
            # trozo que ya es otra entrada); sin el, hasta el primer destino fuera de la funcion
            if t is None or t & 3 or not img.own(t) or (not count and not (start <= t < limit)):
                break
            out.append(t)
    return out


def R(n):
    return '0' if n == 0 else 'c->r[%d]' % n


def S(n):
    return '0' if n == 0 else '(int32_t)c->r[%d]' % n


def setr(n, expr):
    return ('%s = %s;' % (R(n), expr)) if n else ('(void)(%s);' % expr)


def insn(w, pc):
    """C de una instruccion que no es salto"""
    op, rs, rt, rd = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    sh, fn, imm = (w >> 6) & 31, w & 63, w & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    if w == 0:
        return ''
    if op == 0:
        if fn == 0x00: return setr(rd, '%s << %d' % (R(rt), sh))
        if fn == 0x02: return setr(rd, '%s >> %d' % (R(rt), sh))
        if fn == 0x03: return setr(rd, '(uint32_t)(%s >> %d)' % (S(rt), sh))
        if fn == 0x04: return setr(rd, '%s << (%s & 31)' % (R(rt), R(rs)))
        if fn == 0x06: return setr(rd, '%s >> (%s & 31)' % (R(rt), R(rs)))
        if fn == 0x07: return setr(rd, '(uint32_t)(%s >> (%s & 31))' % (S(rt), R(rs)))
        if fn == 0x0C: return 'rc_syscall(c, 0x%08x);' % pc
        if fn == 0x0D: return 'rc_break(c, 0x%08x);' % pc
        if fn == 0x10: return setr(rd, 'c->hi')
        if fn == 0x11: return 'c->hi = %s;' % R(rs)
        if fn == 0x12: return setr(rd, 'c->lo')
        if fn == 0x13: return 'c->lo = %s;' % R(rs)
        if fn == 0x18: return 'rc_mult(c, %s, %s);' % (R(rs), R(rt))
        if fn == 0x19: return 'rc_multu(c, %s, %s);' % (R(rs), R(rt))
        if fn == 0x1A: return 'rc_div(c, %s, %s);' % (R(rs), R(rt))
        if fn == 0x1B: return 'rc_divu(c, %s, %s);' % (R(rs), R(rt))
        if fn in (0x20, 0x21): return setr(rd, '%s + %s' % (R(rs), R(rt)))
        if fn in (0x22, 0x23): return setr(rd, '%s - %s' % (R(rs), R(rt)))
        if fn == 0x24: return setr(rd, '%s & %s' % (R(rs), R(rt)))
        if fn == 0x25: return setr(rd, '%s | %s' % (R(rs), R(rt)))
        if fn == 0x26: return setr(rd, '%s ^ %s' % (R(rs), R(rt)))
        if fn == 0x27: return setr(rd, '~(%s | %s)' % (R(rs), R(rt)))
        if fn == 0x2A: return setr(rd, '(uint32_t)(%s < %s)' % (S(rs), S(rt)))
        if fn == 0x2B: return setr(rd, '(uint32_t)(%s < %s)' % (R(rs), R(rt)))
        return 'rc_illegal(c, 0x%08x);' % pc
    if op in (8, 9): return setr(rt, '%s + 0x%xu' % (R(rs), simm & 0xFFFFFFFF))
    if op == 0x0A: return setr(rt, '(uint32_t)(%s < %d)' % (S(rs), simm))
    if op == 0x0B: return setr(rt, '(uint32_t)(%s < 0x%xu)' % (R(rs), simm & 0xFFFFFFFF))
    if op == 0x0C: return setr(rt, '%s & 0x%xu' % (R(rs), imm))
    if op == 0x0D: return setr(rt, '%s | 0x%xu' % (R(rs), imm))
    if op == 0x0E: return setr(rt, '%s ^ 0x%xu' % (R(rs), imm))
    if op == 0x0F: return setr(rt, '0x%xu' % ((imm << 16) & 0xFFFFFFFF))
    if op == 0x12:
        if w & 0x02000000: return 'rc_gte_cmd(0x%07x);' % (w & 0x1FFFFFF)
        if rs == 0: return setr(rt, 'rc_mfc2(%d)' % rd)
        if rs == 2: return setr(rt, 'rc_cfc2(%d)' % rd)
        if rs == 4: return 'rc_mtc2(%d, %s);' % (rd, R(rt))
        if rs == 6: return 'rc_ctc2(%d, %s);' % (rd, R(rt))
        return 'rc_illegal(c, 0x%08x);' % pc
    if op == 0x10:
        return 'rc_illegal(c, 0x%08x);' % pc
    ea = '%s + 0x%xu' % (R(rs), simm & 0xFFFFFFFF)
    P = '0x%08x' % pc
    if op == 0x20: return setr(rt, '(uint32_t)(int32_t)(int8_t)rc_lb(%s, %s)' % (ea, P))
    if op == 0x21: return setr(rt, '(uint32_t)(int32_t)(int16_t)rc_lh(%s, %s)' % (ea, P))
    if op == 0x23: return setr(rt, 'rc_lw(%s, %s)' % (ea, P))
    if op == 0x24: return setr(rt, 'rc_lb(%s, %s)' % (ea, P))
    if op == 0x25: return setr(rt, 'rc_lh(%s, %s)' % (ea, P))
    if op == 0x22: return setr(rt, 'rc_lwl(%s, %s, %s)' % (R(rt), ea, P))
    if op == 0x26: return setr(rt, 'rc_lwr(%s, %s, %s)' % (R(rt), ea, P))
    if op == 0x28: return 'rc_sb(%s, %s, %s);' % (ea, R(rt), P)
    if op == 0x29: return 'rc_sh(%s, %s, %s);' % (ea, R(rt), P)
    if op == 0x2B: return 'rc_sw(%s, %s, %s);' % (ea, R(rt), P)
    if op == 0x2A: return 'rc_swl(%s, %s, %s);' % (ea, R(rt), P)
    if op == 0x2E: return 'rc_swr(%s, %s, %s);' % (ea, R(rt), P)
    if op == 0x32: return 'rc_mtc2(%d, rc_lw(%s, %s));' % (rt, ea, P)
    if op == 0x3A: return 'rc_sw(%s, rc_mfc2(%d), %s);' % (ea, rt, P)
    return 'rc_illegal(c, 0x%08x);' % pc


def cond(w):
    op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
    if op == 4: return '%s == %s' % (R(rs), R(rt))
    if op == 5: return '%s != %s' % (R(rs), R(rt))
    if op == 6: return '%s <= 0' % S(rs)
    if op == 7: return '%s > 0' % S(rs)
    if op == 1: return ('%s < 0' if rt & 1 == 0 else '%s >= 0') % S(rs)
    raise ValueError


OVL_END = {1: 0x800DB010, 2: 0x800F9E18, 3: 0x800DECE8, 4: 0x800D96C0, 5: 0x800CBA38}
PREFIX = 'f'
STRICT_DATA = False
LABS = set()


class Emitter:
    def __init__(self, img, funcs, resident=()):
        self.img = img
        self.funcs = set(funcs)
        self.resident = set(resident)

    def call(self, tgt, ret=None):
        """llamada; ret = direccion de retorno esperada (jal: pc+8; salto de cola: la de la
        funcion actual, ra0)"""
        exp = 'rc_ret = %s; ' % (('0x%08xu' % ret) if ret is not None else 'ra0')
        if tgt in self.funcs:
            return exp + '%s_%08x(c);' % (PREFIX, tgt)
        if tgt in self.resident:
            return exp + 'f_%08x(c);' % tgt
        if is_lib(tgt):
            return 'rc_libcall(c, 0x%08x);' % tgt
        return exp + 'rc_call(c, 0x%08xu);' % tgt

    def function(self, start, limit):
        img = self.img
        seen, labels, tables = analyze(img, start, limit)
        out = ['void %s_%08x(RCpu *c) {' % (PREFIX, start), '    uint32_t ra0 = rc_ret; RC_BUDGET(0x%08x);' % start]
        pcs = sorted(seen)
        skip = set()
        for i, pc in enumerate(pcs):
            if pc in skip:
                continue
            if pc in labels:
                out.append('L_%08x:;' % pc)
            w = img.word(pc)
            b = branch_info(w, pc)
            if not b:
                s = insn(w, pc)
                if s:
                    out.append('    ' + s)
                nxt = pc + 4
                if nxt not in seen:   # fin del tramo sin salto: cae en la siguiente funcion
                    out.append('    ' + self.call(nxt) + ' return;' if nxt >= limit else '    goto L_%08x;' % nxt)
                    labels.add(nxt)
                continue
            kind, tgt = b
            ds = insn(img.word(pc + 4), pc + 4)
            skip.add(pc + 4)
            n0 = len(out)
            budget = 'RC_BUDGET(0x%08x); ' % pc
            if kind in ('b', 'bal'):
                link = 'c->r[31] = 0x%08xu; ' % (pc + 8) if kind == 'bal' else ''
                back = budget if tgt <= pc else ''
                if start <= tgt < limit:
                    go = 'goto L_%08x;' % tgt
                else:
                    go = '{ %s return; }' % self.call(tgt)
                out.append('    { int t = %s; %s%s if (t) { %s%s } }' % (cond(w), link, ds, back, go))
                if pc + 8 not in seen and pc + 8 >= limit:
                    out.append('    ' + self.call(pc + 8) + ' return;')
            elif kind == 'j':
                if start <= tgt < limit:
                    out.append('    %s %s%s' % (ds, budget if tgt <= pc else '', 'goto L_%08x;' % tgt))
                else:
                    out.append('    %s %s return;' % (ds, self.call(tgt)))
            elif kind == 'jal':
                out.append('    c->r[31] = 0x%08xu; %s %s' % (pc + 8, ds, self.call(tgt, pc + 8)))
                if pc + 8 >= limit:
                    out.append('    ' + self.call(pc + 8) + ' return;')
            elif kind == 'jalr':
                rd = (w >> 11) & 31
                out.append('    { uint32_t t = %s; %s %s rc_ret = 0x%08xu; rc_call(c, t); }'
                           % (R(tgt), ('%s = 0x%08xu;' % (R(rd), pc + 8)) if rd else '', ds, pc + 8))
            elif kind == 'jr':
                if tgt == 31:   # retorno: $ra debe ser el esperado por quien llamo
                    out.append('    { uint32_t t = c->r[31]; %s if (t != ra0) rc_fault(RC_BADRA, 0x%08x, t); return; }' % (ds, pc))
                else:
                    cases = tables.get(pc) or sorted(l for l in labels if l in seen)
                    sw = ' '.join(('case 0x%08xu: goto L_%08x;' % (t, t)) if start <= t < limit else
                                  ('case 0x%08xu: { %s return; }' % (t, self.call(t))) for t in sorted(set(cases)))
                    out.append('    { uint32_t t = %s; %s %sswitch (t) { %s default: rc_ret = ra0; rc_call(c, t); return; } }'
                               % (R(tgt), ds, budget, sw))
            if pc + 4 in labels:   # un salto entra en el hueco de retardo: se ejecuta y se sigue
                if kind in ('b', 'bal', 'jal', 'jalr'):
                    out.append('    goto L_%08x;' % (pc + 8))
                    labels.add(pc + 8)
                out.append('L_%08x:;' % (pc + 4))
                if ds:
                    out.append('    ' + ds)
                if pc + 8 not in seen:
                    out.append('    ' + self.call(pc + 8) + ' return;')
            del n0
        out.append('}')
        # etiquetas que se usan pero no se emitieron (destinos que son hueco de retardo...)
        body = '\n'.join(out)
        used = set(re.findall(r'goto L_([0-9a-f]{8});', body))
        defined = set(re.findall(r'^L_([0-9a-f]{8}):;', body, re.M))
        missing = used - defined
        return body, len(seen), missing


def main():
    global OWN, PREFIX, STRICT_DATA
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    args = sys.argv[1:]
    ovl, resident = None, []
    if '--overlay' in args:
        ovl = int(args[args.index('--overlay') + 1])
        OWN = [(0x800C9DC0, OVL_END[ovl])]
        PREFIX = 'f%d' % ovl
        STRICT_DATA = True
    if '--resident' in args:
        resident = [m['addr'] for m in json.load(open(args[args.index('--resident') + 1]))]
    img = Image(args[0])
    entries, names = find_entries(img, args[1])
    if '--from' in args:   # residente: destinos que le llegan desde los overlays (jal/j y lui/addiu)
        extra = set()
        for path in args[args.index('--from') + 1].split(','):
            o = Image(path)
            last = {}
            for off in range(0, len(o.d) - 3, 4):
                pc = o.base + off
                w = o.word(pc)
                b = branch_info(w, pc)
                if b and b[0] in ('j', 'jal') and img.own(b[1]):
                    extra.add(b[1])
                op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
                if op == 0x0F:
                    last[rt] = imm << 16
                elif op == 0x09 and rs in last:
                    a = (last[rs] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
                    if img.own(a) and not a & 3:
                        extra.add(a)
        entries = sorted(set(entries) | extra)
    out_dir = args[2]
    os.makedirs(out_dir, exist_ok=True)
    # punto fijo: todo destino de salto que sale de una funcion y cae en codigo propio pasa a
    # ser una entrada (el compilador comparte finales de funcion: saltos a mitad de otra)
    entries = set(entries)
    data_done = False
    while True:
        bounds = sorted(entries | {hi for lo, hi in OWN} | {LIB[0]})
        limits = {e: min(b for b in bounds if b > e) for e in entries}
        new, code, table_targets = set(), set(), set()
        for e in entries:
            seen, labels, tables = analyze(img, e, limits[e])
            code |= seen
            for t in tables.values():
                table_targets |= set(t)
                new |= {x for x in t if img.own(x) and not (e <= x < limits[e])}
            last_lui = {}
            for pc in sorted(seen):
                w = img.word(pc)
                b = branch_info(w, pc) if w is not None else None
                if b and b[0] in ('b', 'bal', 'j') and img.own(b[1]) and not (e <= b[1] < limits[e]):
                    new.add(b[1])
                if b and b[0] == 'jal' and img.own(b[1]):
                    new.add(b[1])
                op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
                if op == 0x0F:
                    last_lui[rt] = imm << 16
                elif op == 0x09 and rs in last_lui:   # direccion de codigo cargada con lui/addiu
                    a = (last_lui[rs] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
                    if img.own(a) and not a & 3 and (a in names or a in LABS):
                        new.add(a)
        new -= entries
        if not new and not data_done:
            # punteros a codigo en los DATOS (tablas de callbacks, manejadores...): toda palabra
            # fuera del codigo recorrido que apunte (alineada) al codigo propio, salvo los destinos
            # de tablas de saltos; las funciones que solo se alcanzan asi no estaban recorridas
            data_done = True
            for off in range(0, len(img.d) - 3, 4):
                a = img.base + off
                if a in code:
                    continue
                v = struct.unpack_from('<I', img.d, off)[0]
                if not img.own(v) or v & 3 or v in table_targets or img.word(v) is None:
                    continue
                # residente: el bloque propio es todo codigo; overlays: mezclan codigo y datos, y
                # solo cuenta lo que tiene forma de inicio de funcion
                if not STRICT_DATA or v in names or v in LABS or v in code or img.word(v - 8) == 0x03E00008:
                    new.add(v)
            new -= entries
        if not new:
            break
        entries |= new
    entries = sorted(entries)
    em = Emitter(img, entries, resident)
    stem = os.path.splitext(os.path.basename(args[0]))[0]
    meta, problems = [], []
    with open(os.path.join(out_dir, stem + '.c'), 'w') as f:
        f.write('/* GENERADO por tools/recomp/recomp.py a partir de %s: deriva del codigo de Namco,\n'
                ' * no se distribuye ni entra en git. */\n#include "recomp_rt.h"\n\n' % os.path.basename(args[0]))
        for e in entries:
            f.write('void %s_%08x(RCpu *c);\n' % (PREFIX, e))
        for e in sorted(set(resident) - set(entries)):
            f.write('void f_%08x(RCpu *c);\n' % e)
        f.write('\n')
        for e in entries:
            body, n, missing = em.function(e, limits[e])
            if missing:
                problems.append((e, sorted(missing)))
            f.write(body + '\n\n')
            meta.append({'addr': e, 'name': names.get(e, 'FUN_%08x' % e), 'insns': n, 'ghidra': e in names})
        f.write('const RcFunc rc_funcs_%s[] = {\n' % stem)
        for e in entries:
            f.write('    {0x%08xu, %s_%08x},\n' % (e, PREFIX, e))
        f.write('};\nconst unsigned rc_nfuncs_%s = %d;\n' % (stem, len(entries)))
    json.dump(meta, open(os.path.join(out_dir, stem + '.json'), 'w'), indent=0)
    print('%d funciones, %d instrucciones traducidas -> %s' % (len(entries), sum(m['insns'] for m in meta),
                                                               os.path.join(out_dir, stem + '.c')))
    if problems:
        print('%d funciones con etiquetas sin definir: %s' % (len(problems), problems[:5]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
