#!/usr/bin/env python3
"""psyqcheck_gte — compara las funciones de libgte que usan la GTE con el original.

El oraculo es el codigo de Sony de ACE.BIN ejecutado en tools/mipsemu.py con la GTE
de psx/ (por ctypes); la reimplementacion es psx/src/libgte.c sobre otra instancia
de la misma GTE. Para cada caso: estructuras de entrada aleatorias (bytes de relleno
incluidos) y estado aleatorio de los 64 registros de la GTE, iguales en los dos lados;
se comparan la memoria de salida, el valor devuelto y el estado final de la GTE.

Uso:
    python3 tools/psyqcheck_gte.py data/exe/ace_eu.bin [casos_por_funcion]
"""
import ctypes
import os
import random
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsemu import Machine, PsxGteLib  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = {'M': 32, 'V': 16, 'S': 8, 'L': 4}
# nombre: (direccion EU, argumentos, devuelve valor); 'i' = entero aleatorio.
# InitGeom (0x800A4740) no entra: llama a la BIOS para activar el COP2 (syscall y COP0),
# que el interprete no emula; su parte de GTE son 7 constantes leidas de la descompilacion.
FUNCS = {
    'SetRotMatrix': (0x800A5518, 'M', False),
    'SetTransMatrix': (0x800A5578, 'M', False),
    'SetColorMatrix': (0x800A5548, 'M', False),
    'SetBackColor': (0x800A55C8, 'iii', False),
    'ReadGeomOffset': (0x800A5598, 'LL', False),
    'RotTransPers': (0x800A5828, 'SLLL', True),
    'RotTransPers3': (0x800A5858, 'SSSLLLLL', True),
    'RotTrans': (0x800A58B8, 'SVL', False),
    'MulMatrix0': (0x800A4B08, 'MMM', False),
    'MulMatrix': (0x800A5128, 'MM', False),
    'MulMatrix2': (0x800A5238, 'MM', False),
    'SetMulMatrix': (0x800A4ED8, 'MM', False),
    'ApplyMatrixSV': (0x800A5348, 'MSS', False),
    'ApplyMatrixLV': (0x800A4D78, 'MVV', False),
    'ApplyRotMatrixLV': (0x800A4FE8, 'VV', False),
    'TransposeMatrix': (0x800A5918, 'MM', False),
    'ScaleMatrix': (0x800A53D8, 'MV', False),
    'OuterProduct0': (0x800A57B4, 'VVV', False),
    'RotMatrix': (0x800A5958, 'SM', False),
    'RotMatrixYXZ': (0x800A5BE8, 'SM', False),
    'VectorNormalSS': (0x800A4928, 'SS', True),
}


def in_domain(name, ins_data):
    """Casos que el original trata de forma no definida, contados aparte (no son fallos)."""
    if name == 'VectorNormalSS':
        x, y, z = struct.unpack_from('<3h', ins_data[0])
        return x * x + y * y + z * z < (1 << 31)   # si no, el original lee fuera de su tabla
    return True


def build():
    out = os.path.join(ROOT, 'psx', 'build', 'libpsx.so')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    src = [os.path.join(ROOT, 'psx', 'src', f) for f in ('gte.c', 'libgte.c', 'libgte_math.c', 'libc_psx.c')]
    subprocess.check_call(['cc', '-O2', '-Wall', '-fPIC', '-shared', '-I' + os.path.join(ROOT, 'psx', 'include')]
                          + src + ['-lm', '-o', out])
    return ctypes.CDLL(out)


def rand_struct(rnd, kind):
    n = SIZES[kind]
    if kind == 'M':   # elementos de matriz razonables casi siempre, a veces cualquiera
        el = [rnd.randrange(-4096, 4097) if rnd.random() < 0.8 else rnd.randrange(-32768, 32768) for _ in range(9)]
        return struct.pack('<9h', *el) + bytes(rnd.randrange(256) for _ in range(2)) + \
            struct.pack('<3i', *[rnd.randrange(-20000, 20000) for _ in range(3)])
    if kind == 'V':
        lim = rnd.choice((1000, 40000, 1 << 24, 1 << 30))
        return struct.pack('<4i', *[rnd.randrange(-lim, lim) for _ in range(4)])
    if kind == 'S':
        lim = rnd.choice((300, 4096, 32768))
        return struct.pack('<4h', *[rnd.randrange(-lim, lim) for _ in range(4)])
    return bytes(rnd.randrange(256) for _ in range(n))


def rand_gte(rnd):
    w = [rnd.getrandbits(32) for _ in range(64)]
    w[32 + 26] = rnd.randrange(100, 2000)        # H razonable para que haya proyeccion
    w[32 + 31] = 0
    return struct.pack('<64I', *w)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ncase = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    lib = build()
    oracle = PsxGteLib(lib)
    mine = PsxGteLib(lib)
    lib.psx_gte_set_context(mine.ctx)
    m = Machine.from_acebin(sys.argv[1], gte=oracle)
    rnd = random.Random(42)
    total_bad = 0
    for name, (addr, kinds, has_ret) in FUNCS.items():
        cfunc = getattr(lib, name)
        cfunc.restype = ctypes.c_int32
        bad, example, outside = 0, None, 0
        for _ in range(ncase):
            state = rand_gte(rnd)
            ins, ptrs, args, cbufs, cargs, ins_data = [], [], [], [], [], []
            for k, kind in enumerate(kinds):
                if kind == 'i':
                    v = rnd.randrange(0, 256)
                    args.append(v)
                    cargs.append(ctypes.c_int32(v))
                    continue
                data = rand_struct(rnd, kind)
                ins_data.append(data)
                a = 0x80100000 + 0x40 * k
                m.ram[a & 0x1FFFFF:(a & 0x1FFFFF) + len(data)] = data
                ins.append((a, len(data)))
                args.append(a)
                buf = ctypes.create_string_buffer(data, len(data))
                cbufs.append(buf)
                cargs.append(buf)
            oracle.set_state(state)
            ret_o = m.call(addr, args)
            out_o = [bytes(m.ram[a & 0x1FFFFF:(a & 0x1FFFFF) + n]) for a, n in ins]
            st_o = oracle.state()
            mine.set_state(state)
            ret_c = cfunc(*cargs)
            out_c = [bytes(b.raw) for b in cbufs]
            st_c = mine.state()
            if not has_ret:
                ret_o = ret_c = 0
            if ret_o != ret_c or out_o != out_c or st_o != st_c:
                if not in_domain(name, [bytes(b) for b in ins_data]):
                    outside += 1
                    continue
                bad += 1
                if example is None:
                    diff = [i for i in range(64) if st_o[4 * i:4 * i + 4] != st_c[4 * i:4 * i + 4]]
                    example = 'ret %s/%s, salidas %s, registros distintos %s' % (
                        ret_o, ret_c, [i for i, (x, y) in enumerate(zip(out_o, out_c)) if x != y],
                        ['%s%d' % ('d' if i < 32 else 'c', i % 32) for i in diff])
        total_bad += bad
        extra = ' (+%d fuera del dominio definido)' % outside if outside else ''
        print('%-17s %4d casos, %d diferencias%s%s' % (name, ncase, bad, extra, ('  ej.: ' + example) if bad else ''))
    print('TODO IDENTICO' if not total_bad else 'HAY DIFERENCIAS')
    return 0 if not total_bad else 1


if __name__ == '__main__':
    sys.exit(main())
