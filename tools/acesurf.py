#!/usr/bin/env python3
"""acesurf — superficies moviles del avion del jugador (registro de tipo 4).

Los 12 modelos del preludio (ver acemodel.player_models) se agrupan asi:
  0 tobera, 1 fuselaje, 2-6 alas (5 estados), 7-8 pieza dorsal (2 estados),
  9-11 deriva (3 estados).
El render (scene_objects_sort, 0x80065CC4) pasa los modelos 2-11 como 10
bloques de vertices alternativos y usa por defecto el 0, el 5 y el 7: el
primer estado de cada grupo. Son estados discretos, no una interpolacion.

Esta herramienta mide cada estado frente al primero de su grupo:
  alas: desplazamiento vertical de los vertices con |x| > 0,4 * envergadura, por lado
        (antisimetrico = alabeo; simetrico = cabeceo)
  deriva: desplazamiento lateral (timon)
  pieza dorsal: desplazamiento vertical (aerofreno; 0 = el avion no tiene)

Uso:
    python3 tools/acesurf.py data/bpb/0048_*.bin [mas registros de tipo 4]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acemodel import player_models  # noqa: E402


def span(values):
    return '%+d..%+d' % (min(values), max(values)) if values else '-'


def classify(ms):
    out = []
    base = ms[2].vertices
    lim = 0.4 * max(abs(a[0]) for a in base)
    for k in (3, 4, 5, 6):
        v = ms[k].vertices
        left = [b[1] - a[1] for a, b in zip(base, v) if a[0] < -lim]
        right = [b[1] - a[1] for a, b in zip(base, v) if a[0] > lim]
        sym = (sum(left) > 0) == (sum(right) > 0)
        kind = 'cabeceo' if sym else 'alabeo'
        if not any(left) and not any(right):
            kind = 'sin movimiento'
        out.append('ala %d: dy izq %s der %s -> %s' % (k, span(left), span(right), kind))
    base = ms[9].vertices
    for k in (10, 11):
        dx = [b[0] - a[0] for a, b in zip(base, ms[k].vertices)]
        out.append('deriva %d: dx %s -> %s' % (k, span(dx), 'timon' if any(dx) else 'sin movimiento'))
    dy = [b[1] - a[1] for a, b in zip(ms[7].vertices, ms[8].vertices)]
    has = any(dy)
    out.append('dorsal 7->8: dy %s -> %s' % (span(dy), 'aerofreno' if has else 'sin aerofreno'))
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        raw = open(path, 'rb').read()
        _, ms = player_models(raw)
        ms = dict(ms)
        if len(ms) != 12:
            print('%s: %d modelos, se esperaban 12' % (path, len(ms)))
            continue
        print(os.path.basename(path))
        for line in classify(ms):
            print('  ' + line)


if __name__ == '__main__':
    main()
