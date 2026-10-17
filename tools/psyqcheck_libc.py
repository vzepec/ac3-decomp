#!/usr/bin/env python3
"""psyqcheck_libc — compara la printf de psx/ (psx_format) con la printf de PsyQ.

El interprete ejecuta la printf original del ACE.BIN (0x800AC3E8) y se capturan los
caracteres que entrega a _putchar (0x800ACAC8, desviada a Python antes de su traduccion
de tabuladores y saltos de linea, que es cosa de la consola). psx_format recibe el mismo
formato y los mismos argumentos (palabras de 32 bits; los %s, direcciones en la RAM
compartida). Formatos: los 75 que usa el juego con argumentos aleatorios, y formatos
aleatorios con banderas, ancho, precision, h/l y todas las conversiones.

Uso:
    python3 tools/psyqcheck_libc.py data/exe/ace_eu.bin [casos]
"""
import ctypes
import os
import random
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsemu import Machine  # noqa: E402
from psyqcheck_gpu import build  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRINTF, PUTCHAR, SPRINTF = 0x800AC3E8, 0x800ACAC8, 0x800ACD28
FMT, STRS, ARGS = 0x80180000, 0x80181000, 0x80182000


def game_formats():
    path = os.path.join(ROOT, 'data', 'ghidra', 'all_ace_eu.c')
    if not os.path.exists(path):
        return []
    src = open(path, encoding='latin1').read()
    out = set()
    for f in re.findall(r'\bprintf\("((?:[^"\\]|\\.)*)"', src):
        out.add(f.encode('latin1').decode('unicode_escape').encode('latin1'))
    return sorted(out)


def rand_format(r):
    parts = []
    for _ in range(r.randrange(1, 5)):
        if r.random() < 0.4:
            parts.append(''.join(r.choice('abc XYZ:=-[]') for _ in range(r.randrange(0, 5))))
        flags = ''.join(r.choice('-+ #0') for _ in range(r.choice((0, 0, 1, 2))))
        width = r.choice(('', '', str(r.randrange(1, 12)), '0' + str(r.randrange(1, 9))))
        prec = r.choice(('', '', '.' + str(r.randrange(0, 8)), '.'))
        size = r.choice(('', '', '', 'h', 'l'))
        conv = r.choice('diuxXocsp%')
        parts.append('%' + flags + width + prec + size + conv)
    return ''.join(parts).encode()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    lib = build()
    ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
    m = Machine.from_acebin(sys.argv[1], ram=ram)
    out = []
    m.hooks[PUTCHAR] = lambda mm: out.append(mm.r[4] & 0xFF) or (mm.r[4] & 0xFF)
    r = random.Random(3)
    strs = [b'', b'A', b'hola', b'Electrosphere', b'x' * 20]
    addrs = []
    a = STRS
    for s in strs:
        m.ram[a & 0x1FFFFF:(a & 0x1FFFFF) + len(s) + 1] = s + b'\0'
        addrs.append(a)
        a += 64
    cases = [(f, True) for f in game_formats()] + [(rand_format(r), False) for _ in range(n)]
    buf = ctypes.create_string_buffer(2048)
    bad = bad_game = bad_sprintf = 0
    exs = []
    ex = []
    for fmt, from_game in cases:
        nconv = fmt.count(b'%') + 2
        args = []
        for _ in range(nconv):
            args.append(r.choice((r.choice(addrs), r.getrandbits(32), r.randrange(-1000, 1000) & 0xFFFFFFFF,
                                  r.randrange(32, 127))))
        # %s debe recibir una direccion valida: se fuerza en su posicion
        pos = 0
        for mt in re.finditer(rb'%[-+ #0]*(\*|\d*)(\.(\*|\d*))?[hlL]?(.)', fmt):
            if mt.group(4) == b'%':
                continue
            if mt.group(4) == b's':
                args[pos] = r.choice(addrs)
            pos += 1
        m.ram[FMT & 0x1FFFFF:(FMT & 0x1FFFFF) + len(fmt) + 1] = fmt + b'\0'
        for k, v in enumerate(args):
            m.sw(ARGS + 4 * k, v)
        out.clear()
        regs = [FMT] + args[:3]
        m.call(PRINTF, regs + args[3:])
        want = bytes(out)
        arr = (ctypes.c_uint32 * len(args))(*args)
        k = lib.psx_format(buf, 2048, fmt, arr)
        got = buf.raw[:k]
        arr = (ctypes.c_uint32 * len(args))(*args)
        # sprintf (0x800ACD28): mismo formateador, salida en memoria
        SB = 0x80183000
        m.ram[SB & 0x1FFFFF:(SB & 0x1FFFFF) + 1024] = b'\xAA' * 1024
        m.call(SPRINTF, [SB, FMT] + args[:2] + args[2:])
        k2 = lib.psx_sformat(buf, 2048, fmt, arr)
        want_s = buf.raw[:k2]
        sp = bytes(m.ram[SB & 0x1FFFFF:(SB & 0x1FFFFF) + len(want_s) + 1])
        if sp != want_s + b'\0':
            bad_sprintf += 1
            if len(exs) < 10:
                exs.append((fmt, want_s, bytes(m.ram[SB & 0x1FFFFF:(SB & 0x1FFFFF) + 48]).split(b'\xaa')[0]))
        if got != want:
            bad += 1
            bad_game += from_game
            if len(ex) < 14:
                ex.append((fmt, want, got))
    ng = sum(1 for _, g in cases if g)
    print('printf: %d formatos del juego y %d aleatorios, %d diferencias (%d en formatos del juego)'
          % (ng, len(cases) - ng, bad, bad_game))
    print('sprintf: %d formatos, %d diferencias con psx_sformat' % (len(cases), bad_sprintf))
    bad += bad_sprintf
    for f, w, g in exs:
        print('  sprintf %r: psx %r / PsyQ %r' % (f, w, g))
    for f, w, g in ex:
        print('  %r: PsyQ %r / psx %r' % (f, w, g))
    bad += check_div(m, lib, r, n)
    print('TODO IDENTICO' if not bad else 'HAY DIFERENCIAS')
    return 1 if bad else 0


def check_div(m, lib, r, n):
    """__divdi3 / __udivdi3 (libsn): a en a0:a1, b en a2:a3, cociente en v0:v1"""
    lib.psx_divdi3.restype = ctypes.c_int64
    lib.psx_udivdi3.restype = ctypes.c_uint64
    bad = 0
    for name, addr, signed in (('__divdi3', 0x8009CE80, True), ('__udivdi3', 0x8009CF18, False)):
        nb = 0
        for _ in range(n):
            a = r.choice((r.getrandbits(64), r.getrandbits(33), r.randrange(1 << 20)))
            b = r.choice((r.getrandbits(64), r.getrandbits(32), r.randrange(1, 1000), r.getrandbits(40))) or 1
            m.call(addr, [a & 0xFFFFFFFF, a >> 32, b & 0xFFFFFFFF, b >> 32])
            q_o = (m.r[2] & 0xFFFFFFFF) | (m.r[3] & 0xFFFFFFFF) << 32
            if signed:
                sa = a - (1 << 64) if a >> 63 else a
                sb = b - (1 << 64) if b >> 63 else b
                q_c = lib.psx_divdi3(ctypes.c_int64(sa), ctypes.c_int64(sb)) & 0xFFFFFFFFFFFFFFFF
            else:
                q_c = lib.psx_udivdi3(ctypes.c_uint64(a), ctypes.c_uint64(b))
            nb += q_o != q_c
        print('%-10s %d casos, %d diferencias' % (name, n, nb))
        bad += nb
    return bad


if __name__ == '__main__':
    sys.exit(main())
