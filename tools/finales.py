#!/usr/bin/env python3
"""finales — reproduce las secuencias de final del juego y los creditos (etapa 6, paso 13).

El modo 5 del juego reproduce una secuencia segun g_state + 0x3AC (FUN_800563c8 EU /
FUN_8005658c US, primer paso del modo): 0 = la demostracion que sale si se deja el titulo sin
tocar; 1 y 2 = las secuencias de final que pone FUN_80037d58 segun el nodo siguiente de la
historia (0x2E -> 1; 0x36 o 0x3B -> 2, el fin de la campana, que sigue en los creditos: el paso
FUN_800565c4 devuelve 0x60000007, modo 7 = overlay 3).
Con el binario libre y AC3_SEQ=v, al entrar el juego por primera vez en el modo 5 (la
demostracion del titulo) se escribe v en g_state + 0x3AC. Se comprueba: que se fuerza, que el
juego termina la secuencia y vuelve solo a su ciclo (otra entrada en el modo 5 con 0x3AC = 0),
0 fallos (rc_fault) y salida normal. Capturas cada 600 fotogramas en <salida>/seqV/.

Uso:
    python3 tools/finales.py <imagen_del_disco> <salida> [secuencias] [--eu]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAMES = {'us': 20000, 'eu': 17000}   # la secuencia 2 dura ~14.000 fotogramas a 60 Hz
GUION = {'us': '', 'eu': '320:cruz:6'}   # la europea empieza en el menu de idioma: se elige y se deja el titulo


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ver = 'eu' if '--eu' in sys.argv else 'us'
    argv = [a for a in sys.argv if a != '--eu']
    disc, out = argv[1], argv[2]
    seqs = [int(x) for x in argv[3].split(',')] if len(argv) > 3 else [1, 2]
    exe = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
    bad = 0
    for v in seqs:
        d = os.path.join(out, 'seq%d' % v)
        os.makedirs(d, exist_ok=True)
        p = subprocess.run([exe, disc, '-', str(FRAMES[ver]), d, '600', GUION[ver]], env=dict(os.environ, AC3_SEQ=str(v)),
                           capture_output=True, text=True, errors='replace', cwd=ROOT, timeout=1800)
        log = p.stdout + p.stderr
        open(os.path.join(d, 'log.txt'), 'w').write(log)
        ent = [(int(a), int(f)) for a, f in re.findall(r'secuencia: el juego entra en el modo 5 con 0x3AC = (\d+) en el fotograma (\d+)', log)]
        forced = '  -> %d' % v in log
        back = [f for a, f in ent[1:] if a == 0]
        faults = log.count('recomp: fallo')
        ok = p.returncode == 0 and not faults and forced and back
        bad += not ok
        print('secuencia %d: forzada en el fotograma %s; el juego vuelve a su ciclo en el %s; %d fallos, salida %d -> %s' % (
            v, ent[0][1] if ent else '-', back[0] if back else 'NUNCA', faults, p.returncode, 'bien' if ok else 'FALLA'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
