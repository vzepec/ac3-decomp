#!/usr/bin/env python3
"""acescript — vuelca la cabecera del guion de mision (seccion 6 del tipo 17).

Estado: PARCIAL. Ver docs/04-formatos.md, "Seccion 6 del tipo 17".
La subentrada 0 empieza con 4 contadores (bytes 0, 4, 8, 0xC) y 14 offsets
en palabras a subtablas; la de `DAT_800BAD70` tiene registros de 44 bytes
(tantos como el contador del byte 8) con posiciones en coma fija (<<12).

Uso:
    python3 tools/acescript.py data/bpb/0006_*.bin
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acerec import Record  # noqa: E402
from acemodel import offset_table  # noqa: E402

NAMES = ['ad5c', 'ad7c', 'ad70', 'ad9c', 'adb8', 'adb4', 'ada8', 'adb0',
         'ad74', 'ad98', 'ada0', 'ad90', 'ptr16', 'ptr4']


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    d = Record.from_file(sys.argv[1]).section(6)
    t = offset_table(d)
    print('subentradas de s6:', [b - a for a, b in t])
    a, b = t[0]
    e = d[a:b]
    print('contadores: b0=%d b4=%d b8=%d bC=%d' % (e[0], e[4], e[8], e[0xC]))
    offs = [struct.unpack_from('<i', e, 0x10 + 4 * i)[0] * 4 for i in range(14)]
    for name, o, p in zip(NAMES, offs, offs[1:] + [len(e)]):
        print('  %-6s @%5d  %5d B' % (name, o, p - o))
    o = offs[2]
    print('registros de 44 B (DAT_800BAD70):')
    for i in range(e[8]):
        r = e[o + 44 * i:o + 44 * i + 44]
        x, y, z = struct.unpack_from('<iii', r, 4)
        print('  %2d  bytes %s  pos (%d, %d, %d)' % (i, r[:4].hex(), x >> 12, y >> 12, z >> 12))


if __name__ == '__main__':
    main()
