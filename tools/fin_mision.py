#!/usr/bin/env python3
"""fin_mision — prueba el paso de una mision cumplida al informe (etapa 8: el negro al acabar la Maze).

Con el binario libre (make libre-build) y una tarjeta de memoria con una partida guardada justo antes
de la mision (la del dueno, sacada de la app: data/android/tarjeta/), carga la partida desde el menu,
pasa el informe previo y, ya en vuelo, cierra la mision con AC3_WIN (psx/recomp/native_main.c): lo
mismo que hacen las ordenes 3 y 1 del guion de mision (siguiente parte, resultado) y la peticion del
estado "cumplida". Para la Maze (mision 10 de la US) el guion por defecto es el de sus dos fases:
600:1:0 (seguir al avion espia -> la fabrica) y 1500:1:9 (fabrica destruida -> salir de la mision).

Comprueba que, tras el ultimo cierre, la imagen pasa de negro al informe (DEBRIEFING): en las
capturas de los ultimos fotogramas tiene que haber imagen (no negra). Con la libcd apuntando a las
variables de la europea en la americana (el fallo), el gestor del CD del juego veia "tapa abierta"
y se quedaba esperando con la pantalla en negro.

Uso:
    python3 tools/fin_mision.py <imagen_del_disco> <tarjeta.mcd> <salida> [AC3_WIN]
Devuelve 0 si se llega al informe.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# cargar del menu principal (el de tools/guardado.py, US) y pasar el informe previo de la mision
GUION = ('300:start:8,480:abajo:6,520:cruz:6,700:cruz:6,850:arriba:6,900:cruz:6,1100:cruz:6,1300:cruz:6,'
         + ','.join('%d:cruz:6' % f for f in range(1500, 2101, 150)))
WIN = '600:1:0,1500:1:9'
FRAMES = 5000


def brightness(path):
    with open(path, 'rb') as f:
        data = f.read()
    # PPM P6: tres lineas de cabecera
    body = data.split(b'\n', 3)[3]
    return sum(body[::97]) / max(1, len(body[::97]))


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    disc, card, out = sys.argv[1], sys.argv[2], sys.argv[3]
    win = sys.argv[4] if len(sys.argv) > 4 else WIN
    os.makedirs(out, exist_ok=True)
    copia = os.path.join(out, 'tarjeta.mcd')
    shutil.copyfile(card, copia)   # el juego puede escribir en ella: nunca la original
    exe = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
    env = dict(os.environ, AC3_CARD=copia, AC3_WIN=win)
    p = subprocess.run([exe, disc, '-', str(FRAMES), out, '100', GUION], env=env, capture_output=True, text=True,
                       errors='replace', cwd=ROOT, timeout=1800)
    log = p.stdout + p.stderr
    open(os.path.join(out, 'log.txt'), 'w').write(log)
    for l in log.splitlines():
        if l.startswith('mision'):
            print(l)
    faults = log.count('recomp: fallo')
    ultimos = [os.path.join(out, 'f%05d.ppm' % f) for f in range(FRAMES - 400, FRAMES + 1, 100)]
    luz = [brightness(f) for f in ultimos if os.path.exists(f)]
    print('salida %d, %d fallos; brillo de las ultimas capturas: %s' % (p.returncode, faults,
          ' '.join('%.0f' % b for b in luz)))
    ok = p.returncode == 0 and not faults and luz and min(luz) > 8
    print('RESULTADO: %s' % ('llega al informe' if ok else 'FALLA (pantalla en negro o fallo)'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
