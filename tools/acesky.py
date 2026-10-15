#!/usr/bin/env python3
"""acesky — parametros de cielo y niebla del preludio de una mision (tipo 17).

El preludio esta comprimido con ULZ y descomprime a 312 bytes. Campos
localizados en el codigo (ver docs/04-formatos.md, "Preludio del tipo 17"):

  +0x02 u16  banderas (0x100 -> FUN_80094300, 0x200 -> nubes FUN_80093FB8)
  +0x04 RGB  bajo el horizonte          +0x08 RGB  niebla / horizonte
  +0x10 s16  altura bandas 2 y 3        +0x14/18 RGB cielo alto (sol / espaldas)
  +0x1C RGB  cenit                      +0x20/22 s16 altura bandas 1 y 0
  +0x24/28 RGB cielo bajo (sol / espaldas)
  +0x3E s16  rumbo del sol    +0x6C s16 sol    +0x6E u8 bruma    +0x6F u8 clima

Uso:
    python3 tools/acesky.py data/bpb/0006_*.bin [mas registros...]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acerec import Record  # noqa: E402
import ulz  # noqa: E402


def prelude(path):
    rec = Record.from_file(path)
    raw = open(path, 'rb').read()
    a, b = rec.prelude_bounds()
    d = raw[a:b]
    return ulz.decompress(d) if d[:4] == ulz.MAGIC else d


def sky(p):
    rgb = lambda o: (p[o], p[o + 1], p[o + 2])
    s16 = lambda o: struct.unpack_from('<h', p, o)[0]
    u16 = lambda o: struct.unpack_from('<H', p, o)[0]
    h10 = s16(0x10)
    return {
        'flags': u16(2),
        'below': rgb(4), 'fog': rgb(8),
        'high_sun': rgb(0x14), 'high_away': rgb(0x18),
        'zenith': tuple(c | 3 for c in rgb(0x1C)),
        'low_sun': rgb(0x24), 'low_away': rgb(0x28),
        'band_alt': (-2 * s16(0x22), -2 * s16(0x20), -2 * h10, -h10 // 2, 0),
        'band_dist': (65534, 65534, 61440, 30720, 0x6800),
        'sun_heading': s16(0x3E), 'sun': s16(0x6C),
        'haze': p[0x6E], 'weather': p[0x6F],
    }


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        p = prelude(path)
        if len(p) != 312:
            print('%s: preludio de %d B, no es una mision' % (path, len(p)))
            continue
        s = sky(p)
        hx = lambda c: '#%02x%02x%02x' % c
        print(os.path.basename(path))
        for k in ('zenith', 'high_sun', 'high_away', 'low_sun', 'low_away', 'fog', 'below'):
            print('  %-10s %s' % (k, hx(s[k])))
        print('  bandas: alturas %s  distancias %s' % (s['band_alt'], s['band_dist']))
        print('  sol %d rumbo %d  bruma %d  clima %d  banderas %#06x' % (
            s['sun'], s['sun_heading'], s['haze'], s['weather'], s['flags']))


if __name__ == '__main__':
    main()
