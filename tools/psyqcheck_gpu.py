#!/usr/bin/env python3
"""psyqcheck_gpu — compara las funciones de libgpu que solo escriben en memoria.

Las estructuras se colocan en la MISMA direccion PS1 en la RAM del interprete
(tools/mipsemu.py, codigo original de ACE.BIN) y en la RAM de psx/ (psx_ram), para que
las funciones que guardan direcciones (CatPrim) se comparen tal cual. Variables globales
de la libreria que intervienen: modo de video (0x800B4034) y tamano de VRAM
(0x800B97E8/EA), fijadas igual en los dos lados.

Uso:
    python3 tools/psyqcheck_gpu.py data/exe/ace_eu.bin [casos_por_funcion]
"""
import ctypes
import os
import random
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsemu import Machine  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = {'D': 92, 'P': 20, 'T': 8, 'A': 12, 'R': 8, 'W': 4}
FUNCS = {   # nombre: (direccion EU, argumentos, devuelve valor)
    'SetDefDrawEnv': (0x800AAA98, 'Diiii', True),
    'SetDefDispEnv': (0x800AAB58, 'Piiii', True),
    'SetDrawTPage': (0x800AAC08, 'Tiii', False),
    'SetDrawArea': (0x800A73F0, 'AR', False),
    'CatPrim': (0x800AABD8, 'WW', False),
    'GetTPage': (0x800AAB98, 'iiii', True),
}
BASE = 0x100000


def build():
    out = os.path.join(ROOT, 'psx', 'build', 'libpsx.so')
    src = [os.path.join(ROOT, 'psx', 'src', f) for f in sorted(os.listdir(os.path.join(ROOT, 'psx', 'src')))
           if f.endswith('.c')]
    subprocess.check_call(['cc', '-O2', '-Wall', '-fPIC', '-shared', '-I' + os.path.join(ROOT, 'psx', 'include')]
                          + src + ['-lm', '-o', out])
    return ctypes.CDLL(out)


def rint(rnd):
    return rnd.choice((rnd.randrange(-50, 50), rnd.randrange(-2000, 2000), rnd.randrange(-0x8000, 0x8000),
                       rnd.getrandbits(32) - (1 << 31)))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 500
    lib = build()
    ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
    base_ptr = ctypes.addressof(ram)
    m = Machine.from_acebin(sys.argv[1])
    rnd = random.Random(7)
    total = 0
    for name, (addr, kinds, has_ret) in FUNCS.items():
        f = getattr(lib, name)
        f.restype = ctypes.c_int32
        bad, ex = 0, None
        for _ in range(n):
            mode = rnd.randrange(2)
            m.sw(0x800B4034, mode)
            m.sh(0x800B97E8, 1024)
            m.sh(0x800B97EA, 512)
            lib.SetVideoMode(mode)
            lib.psx_gpu_set_vram_size(1024, 512)
            regions, oargs, cargs = [], [], []
            for k, kind in enumerate(kinds):
                if kind == 'i':
                    v = rint(rnd) if name not in ('SetDrawTPage', 'GetTPage') else rnd.getrandbits(16)
                    oargs.append(v & 0xFFFFFFFF)
                    cargs.append(ctypes.c_int32(v))
                    continue
                off = BASE + 0x100 * k
                data = bytes(rnd.randrange(256) for _ in range(SIZES[kind]))
                if kind == 'R':
                    data = struct.pack('<4h', *[rnd.randrange(-1200, 1200) for _ in range(4)])
                m.ram[off:off + len(data)] = data
                ctypes.memmove(base_ptr + off, data, len(data))
                regions.append((off, len(data)))
                oargs.append(0x80000000 | off)
                cargs.append(ctypes.c_void_p(base_ptr + off))
            r_o = m.call(addr, oargs)
            r_c = f(*cargs)
            if not has_ret:
                r_o = r_c = 0
            elif name in ('SetDefDrawEnv', 'SetDefDispEnv'):   # devuelven el puntero recibido
                r_o, r_c = r_o & 0xFFFFFF, (r_c - (base_ptr & 0xFFFFFFFF)) & 0xFFFFFF
            elif name == 'GetTPage':
                r_o, r_c = r_o & 0xFFFF, r_c & 0xFFFF
            out_o = [bytes(m.ram[o:o + s]) for o, s in regions]
            out_c = [bytes(ram[o:o + s]) for o, s in regions]
            if r_o != r_c or out_o != out_c:
                bad += 1
                if ex is None:
                    ex = 'ret %s/%s, regiones distintas %s' % (r_o, r_c, [i for i, (a, b) in enumerate(zip(out_o, out_c)) if a != b])
        total += bad
        print('%-14s %4d casos, %d diferencias%s' % (name, n, bad, ('  ej.: ' + ex) if bad else ''))
    print('TODO IDENTICO' if not total else 'HAY DIFERENCIAS')
    return 0 if not total else 1


if __name__ == '__main__':
    sys.exit(main())
