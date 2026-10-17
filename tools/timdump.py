#!/usr/bin/env python3
"""timdump — vuelca a archivos .tim los TIM de varios registros (para las pruebas de psx/).

Uso:
    python3 tools/timdump.py <dir_salida> [registro:seccion ...]
Por defecto: mision 6 (s10-s14), registro comun 0000 (s0, s1) y avion 48 (s1).
Los archivos salen del disco del usuario: van a data/ (fuera de git).
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acerec import Record  # noqa: E402
import tim2png  # noqa: E402

DEFAULT = ['6:10', '6:11', '6:12', '6:13', '6:14', '0:0', '0:1', '48:1']


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    n = 0
    for spec in sys.argv[2:] or DEFAULT:
        rec, sec = (int(x) for x in spec.split(':'))
        r = Record.from_file(glob.glob('data/bpb/%04d_*' % rec)[0])
        d = r.section(sec)
        for j, t in enumerate(tim2png.scan(d)):
            o = t.get('offset', t.get('off'))
            end = t.get('end')
            open(os.path.join(out, 'r%d_s%d_%d.tim' % (rec, sec, j)), 'wb').write(d[o:end] if end else d[o:])
            n += 1
    print('%d TIM en %s' % (n, out))


if __name__ == '__main__':
    main()
