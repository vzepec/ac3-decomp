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
SIZES = {'D': 92, 'P': 20, 'T': 8, 'A': 12, 'R': 8, 'W': 4, 'Q': 12}
FUNCS = {   # nombre: (direccion EU, argumentos, devuelve valor)
    'SetDefDrawEnv': (0x800AAA98, 'Diiii', True),
    'SetDefDispEnv': (0x800AAB58, 'Piiii', True),
    'SetDrawTPage': (0x800AAC08, 'Tiii', False),
    'SetDrawArea': (0x800A73F0, 'AR', False),
    'CatPrim': (0x800AABD8, 'WW', False),
    'GetTPage': (0x800AAB98, 'iiii', True),
    'SetDrawMode': (0x800AAC38, 'QiiiR', False),
    'SetDrawMode(tw=0)': (0x800AAC38, 'QiiiZ', False),
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


def check_tim(m, lib, ram, base_ptr, rnd, n):
    """OpenTIM + dos ReadTIM sobre la misma secuencia de TIM (validos, sin CLUT, basura)"""
    lib.ReadTIM.restype = ctypes.c_void_p
    OPEN, READ = 0x800A9218, 0x800A9228
    buf, out = 0x00120000, 0x00128000
    bad = ex = 0
    for _ in range(n):
        data = b''
        for _k in range(2):
            kind = rnd.randrange(4)
            if kind == 3:
                data += struct.pack('<I', rnd.getrandbits(32)) + bytes(rnd.randrange(256) for _ in range(28))
                continue
            mode = rnd.choice((8, 9, 2, 3)) if kind else rnd.getrandbits(32)
            blk = b''
            if mode & 8:
                cw = rnd.randrange(1, 9)
                blk += struct.pack('<I4h', 12 + 2 * 16 * cw, 0, 480, 16, cw) + bytes(32 * cw)
            iw = rnd.randrange(1, 9)
            blk += struct.pack('<I4h', 12 + 2 * iw * 4, 640, 0, iw, 4) + bytes(8 * iw)
            data += struct.pack('<II', 0x10, mode) + blk
        data += b'\0' * 64
        m.ram[buf:buf + len(data)] = data
        ctypes.memmove(base_ptr + buf, data, len(data))
        res_o, res_c = [], []
        m.call(OPEN, [0x80000000 | buf])
        lib.OpenTIM(ctypes.c_void_p(base_ptr + buf))
        for k in range(2):
            o = out + 0x40 * k
            m.ram[o:o + 20] = b'\xAA' * 20
            ctypes.memmove(base_ptr + o + 0x400, b'\xAA' * 20, 20)
            r = m.call(READ, [0x80000000 | o])
            rc = lib.ReadTIM(ctypes.c_void_p(base_ptr + o + 0x400))
            res_o.append(((r & 0xFFFFFF) and (r - o) & 0xFFFFFF, bytes(m.ram[o:o + 20])))
            res_c.append(((rc or 0) and (rc - base_ptr - o - 0x400) & 0xFFFFFF, bytes(ram[o + 0x400:o + 0x414])))
        if res_o != res_c:
            bad += 1
            ex = ex or (res_o, res_c)
    print('%-14s %4d casos, %d diferencias%s' % ('Open/ReadTIM', n, bad, ('  ej.: %s' % (ex,)) if bad else ''))
    return bad


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
        f = getattr(lib, name.split('(')[0])
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
                if kind == 'Z':   # puntero nulo
                    oargs.append(0)
                    cargs.append(ctypes.c_void_p(0))
                    continue
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
    total += check_tim(m, lib, ram, base_ptr, rnd, n)
    print('TODO IDENTICO' if not total else 'HAY DIFERENCIAS')
    return 0 if not total else 1


if __name__ == '__main__':
    sys.exit(main())
