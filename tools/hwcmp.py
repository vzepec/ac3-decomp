#!/usr/bin/env python3
"""hwcmp — compara el dibujo por GLES con la GPU por software en las 41 misiones (etapa 6, paso 17).

Lanza el binario libre (make libre-build: psx/build/ac3_libre) con AC3_HWCMP=<dir> y
AC3_MISSION=N, con el guion de tools/misiones.py (menus, briefing y vuelo con maniobras y
misiles). En cada lista de dibujo la GPU por software dibuja la referencia y GLES (Mesa, escala
1) la vuelve a dibujar; psx/gl/psx_hwcmp.c cuenta los bloques de 8x8 con muchos pixeles muy
distintos ("bloques malos": poligonos estirados, huecos, recortes por mascara...) y guarda
algunas capturas software | GLES | mapa (c<VBlank>.ppm) en <salida>/<N>/.

Uso:
    python3 tools/hwcmp.py <imagen_del_disco> <salida> [misiones] [procesos] [--eu]
HWCMP_GUION=... cambia el guion (formato de tools/misiones.py).
Devuelve 0 si ninguna lista tiene bloques malos.
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import misiones  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOW = re.compile(r'hwcmp: (\d+) listas por debajo del 99 % de casi iguales; la peor, ([\d.]+) %')
SUMMARY = re.compile(r'hwcmp: (\d+) listas comparadas, (\d+) con bloques malos \((\d+) bloques\); '
                     r'pixeles identicos ([\d.]+) %, casi iguales ([\d.]+) %')


def main():
    args = [a for a in sys.argv[1:] if a != '--eu']
    eu = '--eu' in sys.argv
    if len(args) < 2:
        sys.exit(__doc__)
    disc, out = args[0], args[1]
    ms = misiones.parse(args[2]) if len(args) > 2 else list(range(41))
    nproc = int(args[3]) if len(args) > 3 else 2
    guion, frames = (misiones.GUION_EU, misiones.FRAMES_EU) if eu else (misiones.GUION, misiones.FRAMES)
    guion = os.environ.get('HWCMP_GUION', guion)   # otro guion (p. ej. con la vista exterior y el postquemador)
    exe = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
    os.makedirs(out, exist_ok=True)
    pending, running, results = list(ms), {}, {}
    while pending or running:
        while pending and len(running) < nproc:
            m = pending.pop(0)
            d = os.path.join(out, str(m))
            os.makedirs(d, exist_ok=True)
            env = dict(os.environ, AC3_MISSION=str(m), AC3_HWCMP=d, AC3_PAD_ID='73')
            log = open(os.path.join(d, 'log.txt'), 'w')
            running[m] = (subprocess.Popen([exe, disc, '-', str(frames), d, '0', guion], env=env, stdout=log,
                                           stderr=subprocess.STDOUT, cwd=ROOT), log)
        for m, (p, log) in list(running.items()):
            if p.poll() is None:
                continue
            log.close()
            del running[m]
            text = open(os.path.join(out, str(m), 'log.txt'), errors='replace').read()
            s = SUMMARY.search(text)
            lo = LOW.search(text)
            results[m] = (p.returncode, text.count('recomp: fallo'), s.groups() if s else None, lo.groups() if lo else None)
            r = results[m]
            print('mision %2d: salida %d, %d fallos; %s; %s' % (m, r[0], r[1], 'listas %s, con bloques malos %s (%s bloques); '
                  'identicos %s %%, casi iguales %s %%' % r[2] if r[2] else 'SIN RESUMEN',
                  'por debajo del 99 %%: %s (la peor, %s %%)' % r[3] if r[3] else '-'), flush=True)
        if running:
            time.sleep(1)
    lists = sum(int(r[2][0]) for r in results.values() if r[2])
    bad = sum(int(r[2][1]) for r in results.values() if r[2])
    blocks = sum(int(r[2][2]) for r in results.values() if r[2])
    low = sum(int(r[3][0]) for r in results.values() if r[3])
    worst = min((float(r[3][1]) for r in results.values() if r[3]), default=100.0)
    print('TOTAL: %d misiones, %d listas, %d con bloques malos (%d bloques); %d por debajo del 99 %% de casi iguales '
          '(la peor, %.2f %%)' % (len(results), lists, bad, blocks, low, worst))
    return 0 if bad == 0 and all(r[2] for r in results.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
