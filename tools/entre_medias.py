#!/usr/bin/env python3
"""entre_medias — comprueba que cada imagen intermedia quede entre sus dos imagenes reales (etapa 8).

Lee las capturas de la interpolacion con nombre por VBlank (binario libre con AC3_INTERP_NAMEVB=1,
AC3_INTERP_FROM y AC3_INTERP_LEN: i<VBlank>_1-2.ppm la intermedia, i<VBlank>_2-2.ppm la real) y,
para cada intermedia I entre la real anterior A (VBlank - 2) y la suya B, mide la diferencia media
por pixel (gris). La intermedia esta "entre medias" si d(I, A) < d(A, B) y d(I, B) < d(A, B). Con
el avion de la repeticion mal colocado (paso 3), d(I, .) triplicaba a d(A, B).

Uso:
    python3 tools/entre_medias.py <carpeta_de_capturas> [-v]
Devuelve 0 si todas lo estan.
"""
import os
import re
import sys

import numpy as np
from PIL import Image


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    d = sys.argv[1]
    verbose = '-v' in sys.argv
    fs = os.listdir(d)
    real = {int(m.group(1)): f for f in fs for m in [re.match(r'i(\d+)_2-2\.ppm$', f)] if m}
    inter = {int(m.group(1)): f for f in fs for m in [re.match(r'i(\d+)_1-2\.ppm$', f)] if m}

    def load(f):
        try:
            return np.asarray(Image.open(os.path.join(d, f)).convert('L')).astype(float)
        except OSError:   # captura cortada al salir
            return None
    ok = n = cuts = 0
    for v, f in sorted(inter.items()):
        if v - 2 not in real or v not in real:
            continue
        a, b, i = load(real[v - 2]), load(real[v]), load(f)
        if a is None or b is None or i is None or a.shape != b.shape or a.shape != i.shape:
            continue
        dab, dia, dib = np.abs(a - b).mean(), np.abs(i - a).mean(), np.abs(i - b).mean()
        if dib == 0:   # igual que la real: paso sin intermedia (corte de camara)
            cuts += 1
            continue
        good = dia < dab and dib < dab
        n += 1
        ok += good
        if verbose or not good:
            print('%d: d(A,B) %.1f  d(I,A) %.1f  d(I,B) %.1f  %s' % (v, dab, dia, dib, 'entre' if good else 'FUERA'))
    print('entre medias: %d de %d (y %d iguales a su real: cortes)' % (ok, n, cuts))
    return 0 if ok == n else 1


if __name__ == '__main__':
    sys.exit(main())
