#!/usr/bin/env python3
"""interp_holes — mide agujeros (grietas) en las imagenes intermedias de la interpolacion.

Lee las capturas que deja native_main con AC3_INTERP_FROM/AC3_INTERP_LEN (i<paso>_<k>-<den>.ppm)
y, para cada intermedia del paso s, cuenta los pixeles casi negros (canal maximo < 16) cuyo
pixel es claramente no negro (canal maximo >= 40) tanto en la imagen real del paso s-1 como
en la del paso s. Un pixel asi es fondo que asoma entre poligonos que en las dos imagenes
reales lo cubrian: la grieta del terreno que se ve como "terreno negro" al girar rapido.
Validacion (caso positivo conocido): --selftest dibuja un agujero sintetico y debe contarlo.

Uso:
    python3 tools/interp_holes.py <dir_capturas>
    python3 tools/interp_holes.py --selftest
"""
import glob
import os
import re
import sys


def read_ppm(path):
    d = open(path, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def holes(a, b, c):
    """pixeles de b casi negros donde a y c no lo son (bytes RGB del mismo tamano)"""
    n = 0
    for i in range(0, len(b), 3):
        if max(b[i], b[i + 1], b[i + 2]) < 16 and max(a[i], a[i + 1], a[i + 2]) >= 40 and max(c[i], c[i + 1], c[i + 2]) >= 40:
            n += 1
    return n


def selftest():
    w, h = 32, 32
    bright = bytes([100, 100, 100]) * (w * h)
    hole = bytearray(bright)
    for y in range(10, 14):
        for x in range(5, 25):
            o = (y * w + x) * 3
            hole[o:o + 3] = b'\0\0\0'
    got = holes(bright, bytes(hole), bright)
    ok = got == 80 and holes(bright, bright, bright) == 0
    print('selftest: agujero sintetico de 80 pixeles -> %d contados: %s' % (got, 'OK' if ok else 'FALLO'))
    return 0 if ok else 1


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == '--selftest':
        return selftest()
    files = {}
    for p in glob.glob(os.path.join(sys.argv[1], 'i*_*-*.ppm')):
        m = re.match(r'i(\d+)_(\d+)-(\d+)\.ppm', os.path.basename(p))
        if m:
            files[(int(m.group(1)), int(m.group(2)), int(m.group(3)))] = p
    steps = sorted({s for s, k, d in files})
    total, worst, nimg = 0, (0, None), 0
    for s in steps:
        dens = [d for (ss, k, d) in files if ss == s and k == d]
        dprev = [d for (ss, k, d) in files if ss == s - 1 and k == d]
        if not dens or not dprev:
            continue
        den = dens[0]
        a = read_ppm(files[(s - 1, dprev[0], dprev[0])])
        c = read_ppm(files[(s, den, den)])
        if a[:2] != c[:2]:
            continue
        for k in range(1, den):
            if (s, k, den) not in files:
                continue
            b = read_ppm(files[(s, k, den)])
            if b[:2] != a[:2]:
                continue
            n = holes(a[2], b[2], c[2])
            total += n
            nimg += 1
            if n > worst[0]:
                worst = (n, '%d_%d-%d' % (s, k, den))
    print('%d intermedias: %d pixeles de agujero en total (%.1f por imagen); peor %d en %s' %
          (nimg, total, total / max(nimg, 1), worst[0], worst[1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
