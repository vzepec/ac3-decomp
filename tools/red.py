#!/usr/bin/env python3
"""red — el juego en red por lockstep, en el PC (etapa 10, paso 7).

Con el binario libre (make libre-build) y psx/recomp/ac3_net.c. Tres pruebas, todas en la mision 1 (la primera de la
campana; la 0 es la de prueba) y con el cooperativo (AC3_COOP=1):

- red: dos procesos conectados por TCP en este PC (AC3_RED=anfitrion / invitado, puerto 7071). El anfitrion lleva el
  guion de los menus; el invitado (jugador 2, con su vista: AC3_COOP_VER=2) salta la introduccion con START, cambia de
  vista (SELECT), pausa la partida con START y la reanuda el jugador 1. Debe dar: la misma huella de la RAM en cada
  VBlank en los dos (AC3_RAMHASH), 0 huellas distintas en el protocolo, el vuelo antes del VBlank 3000 (sin START, en
  el 6817) y 150 pasos de vuelo menos que sin pausa (los 300 VBlank de pausa).
- control: lo mismo, pero el invitado a 4:3 y el anfitrion a 16:9 (opciones que cambian la partida): el protocolo
  debe avisar y contar huellas distintas (sin este caso positivo, "0 distintas" no probaria nada).
- vistas: sin red, la partida dibujando desde el jugador 1 y desde el 2 (AC3_COOP_VER=1 / 2) con SELECT del 2 y la
  pausa y el menu del 1: la misma huella en cada VBlank (la semilla de rand del redibujo y la pausa vista desde el 2).

Uso:
    python3 tools/red.py <imagen_US> <salida> [red|control|vistas ...]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import coop_vista  # noqa: E402

EXE = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
G0 = coop_vista.G0
FRAMES = 4500
BASE = dict(AC3_PAD_ID='73', AC3_MISSION='1', AC3_COOP='1', AC3_PAD2_ID='73', AC3_WIDE='1', AC3_RED_PUERTO='7071',
            AC3_RED_ESPERA='60')
# el invitado: START en la introduccion, giro, SELECT, pausa en vuelo (la quita el 1 en el 3800)
G2 = '2700:start:6,3100:select:6,3300:lder:60,3500:start:6'
G1 = G0 + ',3800:start:6'


def run(d, env, guion, frames=FRAMES):
    os.makedirs(d, exist_ok=True)
    e = dict(os.environ, **{k: v for k, v in env.items() if v is not None})
    for k in [k for k, v in env.items() if v is None]:   # None: sin la variable (AC3_WIDE vale con solo existir)
        e.pop(k, None)
    log = open(os.path.join(d, 'log.txt'), 'w')
    return subprocess.Popen([EXE, DISC, '-', str(frames), d, '0', guion], env=e, stdout=log, stderr=subprocess.STDOUT,
                            cwd=ROOT)


def log_of(d):
    return open(os.path.join(d, 'log.txt'), errors='replace').read()


def pair(out, host_env, guest_env, g2=G2, g1=G1):
    """anfitrion e invitado conectados; devuelve los dos directorios"""
    h, g = os.path.join(out, 'anfitrion'), os.path.join(out, 'invitado')
    ph = run(h, dict(BASE, AC3_RED='anfitrion', AC3_RAMHASH=os.path.join(h, 'huella.bin'), **host_env), g1)
    pg = run(g, dict(BASE, AC3_RED='invitado', AC3_COOP_VER='2', AC3_GUION2=g2,
                     AC3_RAMHASH=os.path.join(g, 'huella.bin'), **guest_env), G0)
    ph.wait()
    pg.wait()
    return h, g


def net_line(d):
    m = re.search(r'red: (\d+) huellas comparadas, (\d+) distintas', log_of(d))
    return (int(m.group(1)), int(m.group(2))) if m else (0, -1)


def flight(d):
    m = re.search(r'entidad 1 \(\w+\) activa en el VBlank (\d+)', log_of(d))
    return int(m.group(1)) if m else -1


def steps(d):
    m = re.search(r'coop: (\d+) pasos del jugador 2', log_of(d))
    return int(m.group(1)) if m else -1


def test_net(out):
    h, g = pair(os.path.join(out, 'red'), {}, {})
    # la referencia sin pausa: el mismo guion del 2 sin su START en vuelo, y el 1 sin el suyo
    hr, _ = pair(os.path.join(out, 'red_sin_pausa'), {}, {}, g2=G2.rsplit(',', 1)[0], g1=G0)
    same = open(os.path.join(h, 'huella.bin'), 'rb').read() == open(os.path.join(g, 'huella.bin'), 'rb').read()
    cmp, dif = net_line(h)
    fv, s, sr = flight(h), steps(h), steps(hr)
    ok = same and cmp > 0 and dif == 0 and 0 < fv < 3000 and sr - s == 150
    print('red: huellas de cada VBlank %s; protocolo: %d comparadas, %d distintas; vuelo desde el VBlank %d (START del 2); '
          'pasos de vuelo %d, sin pausa %d (la pausa del 2, quitada por el 1: %d menos) -> %s' % (
              'iguales' if same else 'DISTINTAS', cmp, dif, fv, s, sr, sr - s, 'bien' if ok else 'MAL'), flush=True)
    return ok


def test_control(out):
    h, g = pair(os.path.join(out, 'control'), {}, dict(AC3_WIDE=None))
    cmp, dif = net_line(h)
    warned = 'opciones distintas' in log_of(h)
    ok = warned and dif > 0
    print('control (invitado a 4:3): aviso de opciones %s; protocolo: %d comparadas, %d distintas -> %s' % (
        'si' if warned else 'no', cmp, dif, 'bien' if ok else 'MAL'), flush=True)
    return ok


def test_views(out):
    g1 = G0 + ',2700:start:6,3400:start:6,3450:abajo:4,3480:abajo:4,3560:start:6'
    g2 = '2800:lder:200,3100:select:6,3700:select:6'
    ps = []
    for v in (1, 2):
        d = os.path.join(out, 'vistas', 'ver%d' % v)
        env = dict(AC3_PAD_ID='73', AC3_MISSION='1', AC3_COOP='1', AC3_PAD2_ID='73', AC3_WIDE='1', AC3_COMO_APK='1',
                   AC3_SPU='1', AC3_COOP_VER=str(v), AC3_COOP_POS='300,-200', AC3_GUION2=g2,
                   AC3_RAMHASH=os.path.join(d, 'huella.bin'))
        ps.append((d, run(d, env, g1, 4001)))
    for _, p in ps:
        p.wait()
    h = [open(os.path.join(d, 'huella.bin'), 'rb').read() for d, _ in ps]
    m = re.search(r'coop: (\d+) imagenes desde el jugador 2, (\d+) como el original', log_of(ps[1][0]))
    same = h[0] == h[1]
    ok = same and m and int(m.group(2)) == 0
    print('vistas: %d VBlank con SELECT del 2 y pausa y menu del 1, desde el 1 y desde el 2: %s; %s imagenes desde el 2, '
          '%s como el original -> %s' % (len(h[0]) // (35 * 8), 'la misma partida' if same else 'DISTINTAS',
                                        m.group(1) if m else '-', m.group(2) if m else '-', 'bien' if ok else 'MAL'),
          flush=True)
    return ok


def main():
    global DISC
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    DISC, out = sys.argv[1], sys.argv[2]
    what = sys.argv[3:] or ['red', 'control', 'vistas']
    ok = True
    if 'red' in what:
        ok = test_net(out) and ok
    if 'control' in what:
        ok = test_control(out) and ok
    if 'vistas' in what:
        ok = test_views(out) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
