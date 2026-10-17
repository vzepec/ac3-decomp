#!/usr/bin/env python3
"""psxcheck_display — salida de video de psx/ (display.c) con datos reales.

Peliculas del ACE.SPB montadas en la VRAM como lo hace el reproductor del juego
(0x800555C0 / 0x8005587C): DecDCTvlc2, DecDCTin en 24 bits, DecDCTout de columnas de 16
pixeles (24 medias palabras de ancho) y LoadImage de cada columna en x += 24. Con
PutDispEnv en modo de 24 bits y SetDispMask(1), psx_display_frame debe dar exactamente el
RGB que sale del MDEC. Escribe una imagen de muestra (PNG) si se pide.

Uso:
    python3 tools/psxcheck_display.py <imagen.bin> [fotogramas_por_pelicula] [muestra.png]
"""
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psyqcheck_gpu import build  # noqa: E402
from psyqcheck_press import movie_frames  # noqa: E402
from tim2png import write_png  # noqa: E402


class RECT(ctypes.Structure):
    _fields_ = [('x', ctypes.c_int16), ('y', ctypes.c_int16), ('w', ctypes.c_int16), ('h', ctypes.c_int16)]


class DISPENV(ctypes.Structure):
    _fields_ = [('disp', RECT), ('screen', RECT), ('isinter', ctypes.c_uint8), ('isrgb24', ctypes.c_uint8),
                ('pad0', ctypes.c_uint8), ('pad1', ctypes.c_uint8)]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    image = sys.argv[1]
    nper = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    sample = sys.argv[3] if len(sys.argv) > 3 else None
    lib = build()
    ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
    base = ctypes.addressof(ram)
    lib.ResetGraph(0)
    lib.SetDispMask(1)
    lib.DecDCTReset(0)
    out = (ctypes.c_uint32 * (1024 * 512))()
    fw, fh = ctypes.c_int(), ctypes.c_int()
    bad = total = 0
    for e in (0, 1):
        for fn, w, h, data in movie_frames(image, e, nper, step=60):
            BS, RL, SL = 0x100000, 0x120000, 0x160000
            ctypes.memmove(base + BS, data, len(data))
            lib.DecDCTvlc2(ctypes.c_void_p(base + BS), ctypes.c_void_p(base + RL), None)
            lib.DecDCTin(ctypes.c_void_p(base + RL), 1)
            mbw, mbh = (w + 15) // 16, (h + 15) // 16
            ref = bytearray(mbw * 16 * mbh * 16 * 3)
            x0, y0 = 0, 16
            for mx in range(mbw):   # una columna de macrobloques por DecDCTout, como el juego
                words = mbh * 16 * 16 * 3 // 4
                lib.DecDCTout(ctypes.c_void_p(base + SL), words)
                col = bytes(ram[SL:SL + words * 4])
                r = RECT(x0 + mx * 24, y0, 24, mbh * 16)
                lib.LoadImage(ctypes.byref(r), ctypes.c_void_p(base + SL))
                for my in range(mbh):
                    mb = col[my * 768:(my + 1) * 768]
                    for py in range(16):
                        o = ((my * 16 + py) * mbw * 16 + mx * 16) * 3
                        ref[o:o + 48] = mb[py * 48:py * 48 + 48]
            d = DISPENV(RECT(x0, y0, w, h), RECT(0, 0, 0, 0), 0, 1, 0, 0)
            lib.PutDispEnv(ctypes.byref(d))
            lib.psx_display_frame(out, 1024 * 512, ctypes.byref(fw), ctypes.byref(fh))
            got = bytes(ctypes.string_at(ctypes.addressof(out), w * h * 4))
            want = bytearray()
            for y in range(h):
                for x in range(w):
                    o = (y * mbw * 16 + x) * 3
                    want += ref[o:o + 3] + b'\xff'
            total += 1
            same = (fw.value, fh.value) == (w, h) and got == bytes(want)
            bad += not same
            print('pelicula %d fotograma %d (%dx%d, 24 bits): %s' % (e, fn, w, h, 'IDENTICO al MDEC' if same else 'DISTINTO'))
            if sample and e == 0 and fn and not os.path.exists(sample):
                write_png(sample, w, h, got)
    print('%d fotogramas; %s' % (total, 'TODO BIEN' if not bad else 'HAY FALLOS (%d)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
