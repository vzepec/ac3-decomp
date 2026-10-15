#!/usr/bin/env python3
"""acescript — vuelca la cabecera del guion de mision (seccion 6 del tipo 17).

Ver docs/04-formatos.md, "Seccion 6 del tipo 17". La subentrada 0 empieza
con 4 contadores (bytes 0, 4, 8, 0xC) y 14 offsets en palabras a subtablas.
Tablas de aparicion (FUN_8004C258 / FUN_8004CDB0):
  DAT_800BAD70: aviones, contador del byte 8, registros de 44 B
  DAT_800BAD74: objetos, contador del byte 0xC, registros de 36 B
  byte 0 bando, byte 1 tipo (entidad +0x48), byte 2 modelo (entidad +4),
  bytes 4-0x13 posicion (<<12) y orientacion (entidad +8).

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
    for title, o, n, size in (('aviones (DAT_800BAD70, ranuras 0-31)', offs[2], e[8], 44),
                              ('objetos (DAT_800BAD74, ranuras 32-63)', offs[8], e[0xC], 36)):
        print('%s: %d registros de %d B' % (title, n, size))
        for i in range(n):
            r = e[o + size * i:o + size * (i + 1)]
            x, y, z = struct.unpack_from('<iii', r, 4)
            print('  %2d  bando %d  tipo %#04x  modelo %3d  b3 %3d  pos (%d, %d, %d)' % (
                i, r[0], r[1], r[2], r[3], x >> 12, y >> 12, z >> 12))


if __name__ == '__main__':
    main()
