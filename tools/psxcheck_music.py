#!/usr/bin/env python3
"""psxcheck_music — comprueba la musica XA de la rebanada de Android (android/src/music.c).

android/test/test_music.c escribe lo que la rebanada entrega al mezclador para la pista
de una mision. Aqui se calcula lo mismo por otro camino: la pista con tools/acemusic.py
(tablas del ACE.BIN), su entrada y canal con tools/acepack.py, los sectores de audio del
canal y la decodificacion con psx_xa_decode44 (el decodificador validado contra libav en
tools/psxcheck_xa.py). Deben coincidir muestra a muestra.

Uso:
    python3 tools/psxcheck_music.py <imagen.bin> <registro> <salida_de_test_music.raw>
"""
import ctypes
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acemusic import table  # noqa: E402
from acepack import load, spb_tag  # noqa: E402
from psxiso import Disc  # noqa: E402
from psyqcheck_gpu import build  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    image, rec, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    got = open(path, 'rb').read()
    got = list(struct.unpack('<%dh' % (len(got) // 2), got))
    out, _ = table(os.path.join(ROOT, 'data', 'exe', 'ace_eu.bin'))
    entry = [r for rc, _, r, _, _ in out if rc == rec][0] & 0x7FF
    idx, pack = load(Disc(image), 'SPB')
    tag, sec = idx.entries[entry]
    t = spb_tag(tag)
    lib = build()
    x = ctypes.create_string_buffer(64)
    lib.psx_xa_reset(x)
    buf = (ctypes.c_int16 * 12000)()
    ref = []
    f = open(image, 'rb')
    k = 0
    while len(ref) < len(got) and k < t['sectors']:
        f.seek((pack.lba + sec + k) * 2352)
        raw = f.read(2352)
        k += 1
        if raw[15] == 2 and raw[17] == t['channel'] and raw[18] & 4:
            n = lib.psx_xa_decode44(x, raw, buf, 6000)
            ref += buf[:2 * n]
    # el mezclador puede quedarse sin datos al arrancar (devuelve silencio): se alinea
    start = next(i for i, v in enumerate(got) if v != 0) & ~1
    first = next(i for i, v in enumerate(ref) if v != 0) & ~1
    a, b = got[start:], ref[first:first + len(got) - start]
    n = min(len(a), len(b))
    same = sum(1 for i in range(n) if a[i] == b[i])
    gaps = sum(1 for i in range(0, n - 2, 2) if a[i] == 0 and a[i + 1] == 0 and b[i] != 0)
    print('registro %d -> entrada %d del SPH (canal %d): %d/%d muestras identicas a la decodificacion directa, '
          '%d huecos de silencio' % (rec, entry, t['channel'], same, n, gaps))
    ok = same == n
    print('TODO BIEN' if ok else 'HAY DIFERENCIAS')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
