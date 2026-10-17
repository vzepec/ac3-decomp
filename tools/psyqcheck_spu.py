#!/usr/bin/env python3
"""psyqcheck_spu — compara libspu de psx/ con la libreria original del ACE.BIN.

El interprete (tools/mipsemu.py) ejecuta las funciones originales de libspu; sus
accesos a los registros de la SPU (0x1F801C00...) van a una instancia de la SPU por
software de psx/ (psx_spu_read16/write16 por ctypes). La reimplementacion en C escribe
en otra instancia (la de la libreria, psx_spu()). Tras cada llamada se comparan los 512
registros, la envolvente de las 24 voces y el valor devuelto; para SpuGetAllKeysStatus,
el vector de 24 estados.

Estado interno de la original que interviene, igual en los dos lados: modo de memoria
(0x800B2668-74, valores de _spu_init), teclas pulsadas (0x800B25D0), notas de muestra
(0x800B2600). En SpuSetReverb(1) la original consulta la reserva de la zona de
reverberacion (0x800B25DC); se pone a 1 (reservada), que es el caso que no comprueba.

Uso:
    python3 tools/psyqcheck_spu.py data/exe/ace_eu.bin [casos_por_funcion]
"""
import ctypes
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsemu import Machine  # noqa: E402
from psyqcheck_gpu import build  # noqa: E402

F = {'SpuSetVoiceAttr': 0x8009FD68, 'SpuSetCommonAttr': 0x8009F888, 'SpuSetKey': 0x8009F3C8,
     'SpuGetAllKeysStatus': 0x8009FCDC, '_spu_note2pitch': 0x800A0368, 'SpuSetReverb': 0x8009E2B8,
     'SpuSetReverbDepth': 0x8009EE48, 'SpuSetReverbVoice': 0x8009EEC8}
ARG = 0x80180000   # estructura de argumentos (misma direccion en los dos lados)
OUT = 0x80180100


class SpuIO:
    """registros de E/S del interprete: la SPU va a una instancia de psx/; el resto, a un dict"""
    def __init__(self, lib, spu):
        self.lib, self.spu, self.other = lib, spu, {}

    def read(self, a, size):
        if 0x1F801C00 <= a < 0x1F802000:
            v = self.lib.psx_spu_read16(self.spu, a - 0x1F801C00)
            if size == 4:
                v |= self.lib.psx_spu_read16(self.spu, a - 0x1F801C00 + 2) << 16
            return v
        return self.other.get(a, 0)

    def write(self, a, size, v):
        if 0x1F801C00 <= a < 0x1F802000:
            self.lib.psx_spu_write16(self.spu, a - 0x1F801C00, v & 0xFFFF)
            if size == 4:
                self.lib.psx_spu_write16(self.spu, a - 0x1F801C00 + 2, v >> 16)
        else:
            self.other[a] = v


def rnd16(r):
    return r.choice((r.randrange(0, 128), r.randrange(-200, 200) & 0xFFFF, r.getrandbits(16)))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    lib = build()
    ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
    base = ctypes.addressof(ram)
    lib.psx_spu_sizeof.restype = ctypes.c_size_t
    lib.psx_spu.restype = ctypes.c_void_p
    lib.psx_spu_read16.restype = ctypes.c_uint16
    lib._spu_note2pitch.restype = ctypes.c_uint16
    for name in ('SpuSetReverb', 'SpuSetReverbDepth'):
        getattr(lib, name).restype = ctypes.c_int32
    lib.SpuSetReverbVoice.restype = ctypes.c_uint32
    ospu = ctypes.create_string_buffer(lib.psx_spu_sizeof())
    ospu_p = ctypes.c_void_p(ctypes.addressof(ospu))
    lib.psx_spu_reset(ospu_p)
    m = Machine.from_acebin(sys.argv[1], ram=ram)
    m.io = SpuIO(lib, ospu_p)
    m.sw(0x800B25DC, 1)
    lib.SpuInit()
    cspu = ctypes.c_void_p(lib.psx_spu())
    ctypes.memmove(ospu, cspu, 0x400)    # mismo estado inicial de registros
    rd = lib.psx_spu_read16

    def state(spu):
        return bytes(ctypes.string_at(spu.value if isinstance(spu, ctypes.c_void_p) else spu, 0x400)) + \
            bytes(struct.pack('<24H', *[rd(spu, 16 * i + 12) for i in range(24)]))

    def put(data):
        m.ram[(ARG & 0x1FFFFF):(ARG & 0x1FFFFF) + len(data)] = data

    r = random.Random(5)
    total = 0
    for name, addr in F.items():
        bad, ex = 0, None
        for _ in range(n):
            args_o, args_c, ret_kind = [], [], 'v'
            if name == 'SpuSetVoiceAttr':
                mask = r.choice((0, r.getrandbits(19), 0x23, 0x600E3, 1 << r.randrange(19)))
                f = struct.pack('<II6h', r.getrandbits(24) if r.random() < .5 else 1 << r.randrange(24), mask,
                                *[struct.unpack('<h', struct.pack('<H', rnd16(r)))[0] for _ in range(2)],
                                r.randrange(-1, 9), r.randrange(-1, 9), 0, 0)
                f += struct.pack('<4H', rnd16(r), r.getrandbits(16), r.getrandbits(16) if r.random() < .3 else
                                 r.randrange(0x3000, 0x5000), 0)
                f += struct.pack('<II3i', r.getrandbits(19), r.getrandbits(19), *[r.randrange(0, 9) for _ in range(3)])
                f += struct.pack('<7H', *[r.choice((r.randrange(0, 40), r.getrandbits(16))) for _ in range(5)],
                                 r.getrandbits(16), r.getrandbits(16))
                put(f + b'\0' * 2)
                args_o, args_c = [ARG], [ctypes.c_void_p(base + (ARG & 0x1FFFFF))]
            elif name == 'SpuSetCommonAttr':
                f = struct.pack('<I6h', r.choice((0, r.getrandbits(14))), *[struct.unpack('<h', struct.pack('<H', rnd16(r)))[0]
                                                                            for _ in range(2)],
                                r.randrange(-1, 9), r.randrange(-1, 9), 0, 0)
                f += struct.pack('<2hii2hii', r.getrandbits(15), r.getrandbits(15), r.randrange(2), r.randrange(2),
                                 r.getrandbits(15), r.getrandbits(15), r.randrange(2), r.randrange(2))
                put(f)
                args_o, args_c = [ARG], [ctypes.c_void_p(base + (ARG & 0x1FFFFF))]
            elif name == 'SpuSetKey':
                oo, vb = r.choice((0, 1, 1, 2)), r.getrandbits(32)
                args_o, args_c = [oo, vb], [ctypes.c_int32(oo), ctypes.c_uint32(vb)]
            elif name == 'SpuGetAllKeysStatus':
                for i in range(24):   # envolventes al azar en los dos lados
                    lv = r.choice((0, 0, r.randrange(1, 0x8000)))
                    lib.psx_spu_write16(ospu_p, 16 * i + 12, lv)
                    lib.psx_spu_write16(cspu, 16 * i + 12, lv)
                oo, vb = r.randrange(2), r.getrandbits(24)
                m.call(F['SpuSetKey'], [oo, vb])
                lib.SpuSetKey(ctypes.c_int32(oo), ctypes.c_uint32(vb))
                args_o, args_c = [OUT], [ctypes.c_void_p(base + (OUT & 0x1FFFFF) + 0x40)]
            elif name == '_spu_note2pitch':
                v = [r.randrange(256) for _ in range(4)]
                args_o, args_c, ret_kind = v, [ctypes.c_int(x) for x in v], 'h'
            elif name == 'SpuSetReverb':
                oo = r.choice((0, 1, 2, -1))
                args_o, args_c, ret_kind = [oo], [ctypes.c_int32(oo)], 'i'
            elif name == 'SpuSetReverbDepth':
                put(struct.pack('<Ii2hii', r.choice((0, r.getrandbits(3))), 0, r.getrandbits(15), r.getrandbits(15), 0, 0))
                args_o, args_c, ret_kind = [ARG], [ctypes.c_void_p(base + (ARG & 0x1FFFFF))], 'i'
            elif name == 'SpuSetReverbVoice':
                oo, vb = r.choice((0, 1, 8, 3)), r.getrandbits(32)
                args_o, args_c = [oo, vb], [ctypes.c_int32(oo), ctypes.c_uint32(vb)]
            ro = m.call(addr, args_o)
            rc = getattr(lib, name)(*args_c)
            if ret_kind == 'h':
                ro, rc = ro & 0xFFFF, rc & 0xFFFF
            elif ret_kind == 'i':
                ro, rc = ro, rc
            else:
                ro = rc = 0
            so, sc = state(ospu_p), state(cspu)
            extra = True
            if name == 'SpuGetAllKeysStatus':
                a = OUT & 0x1FFFFF
                extra = bytes(m.ram[a:a + 24]) == bytes(m.ram[a + 0x40:a + 0x40 + 24])
            if ro != rc or so != sc or not extra:
                bad += 1
                if ex is None:
                    diff = [i for i in range(0, len(so), 2) if so[i:i + 2] != sc[i:i + 2]][:4]
                    ex = 'ret %s/%s, registros distintos %s %s' % (ro, rc, [hex(d) for d in diff],
                                                                    [(so[d:d + 2].hex(), sc[d:d + 2].hex()) for d in diff])
                ctypes.memmove(ospu, cspu, 0x400)   # se resincroniza para seguir contando
        total += bad
        print('%-20s %5d casos, %d diferencias%s' % (name, n, bad, ('  ej.: ' + ex) if bad else ''))
    print('TODO IDENTICO' if not total else 'HAY DIFERENCIAS')
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
