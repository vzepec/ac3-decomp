#!/usr/bin/env python3
"""estados — captura estados reales de las funciones pendientes de validar (etapa 6, paso 12).

Recorre las misiones con el binario libre (como tools/misiones.py) y, con AC3_STATES, guarda el
estado completo (registros, GTE, scratchpad y RAM) la primera vez que el juego entra en cada
funcion de la lista. Tras cada mision quita de la lista las que ya tienen estado, para no repetir.
La lista sale de tools/cobertura.py (funciones ejecutadas en partida que el comparador con estados
aleatorios dejo SIN CASO). Los estados son RAM del juego: van al directorio de salida (fuera de
git) y los consume tools/recomp/check.py --states.

Uso:
    python3 tools/estados.py <imagen_del_disco> <cobertura.json> <salida> [misiones] [por_funcion] [--eu]
"""
import glob
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from misiones import GUION, FRAMES, GUION_EU, FRAMES_EU, ROOT, parse  # noqa: E402


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    argv = [a for a in sys.argv if a != '--eu']
    guion, frames = (GUION_EU, FRAMES_EU) if '--eu' in sys.argv else (GUION, FRAMES)
    disc, cov, out = argv[1], argv[2], argv[3]
    ms = parse(argv[4]) if len(argv) > 4 else list(range(41))
    per = int(argv[5]) if len(argv) > 5 else 1
    want = {}
    for mod, d in json.load(open(cov)).items():
        for a in d['ejecutadas_sin_caso']:
            want[int(a, 16)] = want.get(int(a, 16), 0)
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
    for m in ms:
        have = {}
        for p in glob.glob(os.path.join(out, 'st_*.bin')):
            a = int(re.match(r'st_([0-9a-f]{8})_', os.path.basename(p)).group(1), 16)
            have[a] = have.get(a, 0) + 1
        todo = sorted(a for a in want if have.get(a, 0) < per)
        if not todo:
            break
        lst = os.path.join(out, 'lista.txt')
        open(lst, 'w').write(''.join('%08x\n' % a for a in todo))
        d = os.path.join(out, 'mision%d' % m)
        os.makedirs(d, exist_ok=True)
        env = dict(os.environ, AC3_PAD_ID='73', AC3_MISSION=str(m), AC3_STATES=out, AC3_STATES_LIST=lst,
                   AC3_STATES_MAX=str(per))
        with open(os.path.join(d, 'log.txt'), 'w') as log:
            code = subprocess.call([exe, disc, '-', str(frames), d, '100000', guion], env=env, stdout=log,
                                   stderr=subprocess.STDOUT, cwd=ROOT, timeout=900)
        n = len(glob.glob(os.path.join(out, 'st_*.bin')))
        print('mision %2d: salida %d, %d estados en total, quedaban %d funciones' % (m, code, n, len(todo)), flush=True)
    have = {int(re.match(r'st_([0-9a-f]{8})_', os.path.basename(p)).group(1), 16)
            for p in glob.glob(os.path.join(out, 'st_*.bin'))}
    print('%d de %d funciones con estado real' % (len(have & set(want)), len(want)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
