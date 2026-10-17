#!/usr/bin/env python3
"""psyqcheck_press — compara DecDCTvlc2 de psx/ con la de libpress del ACE.BIN.

Fotogramas reales de las peliculas del ACE.SPB (entradas de modo 3): sectores de video
con la cabecera de 32 bytes de Namco (byte 1 = trozo, u16 2 = trozos por fotograma,
u16 6 = numero de fotograma, u16 8/10 = ancho/alto) y 2016 bytes de datos cada uno. El
interprete ejecuta DecDCTvlcBuild (0x800AB6E8), DecDCTvlcSize2(0) y DecDCTvlc2
(0x800AB398) originales; psx/ decodifica el mismo fotograma y se comparan los codigos
run-level (cabecera + 2 * nnnn palabras de 16 bits).

Uso:
    python3 tools/psyqcheck_press.py <imagen.bin> data/exe/ace_eu.bin [fotogramas_por_pelicula]
"""
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acepack import load, spb_tag  # noqa: E402
from mipsemu import Machine  # noqa: E402
from psxiso import Disc  # noqa: E402
from psyqcheck_gpu import build  # noqa: E402

BUILD, SIZE2, VLC2 = 0x800AB6E8, 0x800AB368, 0x800AB398
TAB, BS, OUT, OUT_C = 0x80100000, 0x80140000, 0x80150000, 0x80180000


def movie_frames(image, entry, maxf, step=1):
    """[(numero, ancho, alto, datos)] de la pelicula `entry` del SPB"""
    d = Disc(image)
    idx, pack = load(d, 'SPB')
    f = open(image, 'rb')
    tag, sec = idx.entries[entry]
    t = spb_tag(tag)
    cur, out, seen = {}, [], 0
    for k in range(t['sectors']):
        f.seek((pack.lba + sec + k) * 2352)
        raw = f.read(2352)
        if raw[15] != 2 or raw[18] & 4 or not raw[18] & 8:
            continue
        _, ci, nch, _, fn, w, h = struct.unpack_from('<BBHHHHH', raw, 24)
        if not nch or ci >= nch:
            continue
        cur.setdefault(fn, {})[ci] = raw[56:56 + 2016]
        if len(cur[fn]) == nch:
            data = b''.join(cur[fn][i] for i in range(nch))
            del cur[fn]
            if data[2:4] != b'\x00\x38' or data[6] not in (2, 3):   # no es un fotograma MDEC
                continue
            seen += 1
            if seen % step == 0:
                out.append((fn, w, h, data))
            if len(out) >= maxf:
                break
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    image, acebin = sys.argv[1], sys.argv[2]
    nper = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    lib = build()
    ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
    base = ctypes.addressof(ram)
    m = Machine.from_acebin(acebin, ram=ram)
    m.call(BUILD, [TAB])
    m.call(SIZE2, [0])
    idx, _ = load(Disc(image), 'SPB')
    movies = [i for i, (tg, _) in enumerate(idx.entries) if spb_tag(tg)['mode'] == 3]
    total = bad = 0
    ex = None
    for e in movies:
        frs = movie_frames(image, e, nper, step=7)
        nb = 0
        for fn, w, h, data in frs:
            a = BS & 0x1FFFFF
            m.ram[a:a + len(data) + 64] = data + b'\0' * 64
            count = struct.unpack_from('<H', data, 0)[0]
            n = 4 + count * 4
            for o in (OUT, OUT_C):
                m.ram[o & 0x1FFFFF:(o & 0x1FFFFF) + n + 16] = b'\xAA' * (n + 16)
            m.call(VLC2, [BS, OUT, TAB], max_steps=60000000)
            lib.DecDCTvlc2(ctypes.c_void_p(base + a), ctypes.c_void_p(base + (OUT_C & 0x1FFFFF)),
                           ctypes.c_void_p(base + (TAB & 0x1FFFFF)))
            o1 = bytes(m.ram[OUT & 0x1FFFFF:(OUT & 0x1FFFFF) + n])
            o2 = bytes(m.ram[OUT_C & 0x1FFFFF:(OUT_C & 0x1FFFFF) + n])
            total += 1
            if o1 != o2:
                nb += 1
                if ex is None:
                    i = next(k for k in range(0, n, 2) if o1[k:k + 2] != o2[k:k + 2])
                    ex = (e, fn, data[6], i, o1[max(0, i - 8):i + 8].hex(' ', 2), o2[max(0, i - 8):i + 8].hex(' ', 2))
        bad += nb
        if frs and len(frs) > 0:
            print('pelicula %2d: %2d fotogramas %dx%d version %d, %d distintos'
                  % (e, len(frs), frs[0][1], frs[0][2], frs[0][3][6], nb))
    print('%d fotogramas, %d distintos%s' % (total, bad, ('\n  ej.: pelicula %d fotograma %d v%d, byte %d\n'
                                                          '  original %s\n  psx      %s' % ex) if ex else ''))
    print('TODO IDENTICO' if not bad else 'HAY DIFERENCIAS')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
