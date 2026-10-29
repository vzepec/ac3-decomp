#!/usr/bin/env python3
"""red — el juego en red por lockstep, en el PC (etapa 10, pasos 7 y 8).

Con el binario libre (make libre-build) y psx/recomp/ac3_net.c. Pruebas en la mision 1 (la primera de la campana; la
0 es la de prueba) y con el cooperativo (AC3_COOP=1):

- red: dos procesos conectados por TCP en este PC (AC3_RED=anfitrion / invitado, puerto 7071). El anfitrion elige la
  mision (AC3_MISSION=1) y el invitado, sin ella, la recibe en el saludo (paso 8). El anfitrion lleva el
  guion de los menus; el invitado (jugador 2, con su vista: AC3_COOP_VER=2) salta la introduccion con START, cambia de
  vista (SELECT), pausa la partida con START y la reanuda el jugador 1. Debe dar: la misma huella de la RAM en cada
  VBlank en los dos (AC3_RAMHASH), 0 huellas distintas en el protocolo, el vuelo antes del VBlank 3000 (sin START, en
  el 6817) y 150 pasos de vuelo menos que sin pausa (los 300 VBlank de pausa).
- control: lo mismo, pero el invitado a 4:3 y el anfitrion a 16:9 (opciones que cambian la partida): el protocolo
  debe avisar y contar huellas distintas (sin este caso positivo, "0 distintas" no probaria nada).
- vistas: sin red, la partida dibujando desde el jugador 1 y desde el 2 (AC3_COOP_VER=1 / 2) con SELECT del 2 y la
  pausa y el menu del 1: la misma huella en cada VBlank (la semilla de rand del redibujo y la pausa vista desde el 2).
- anuncio (paso 8): mientras el anfitrion espera, su anuncio por UDP (el que lee la lista de partidas del APK): se
  escucha en el puerto 7071 y debe llegar cada segundo con su puerto, su disco, su mision y su nombre.
- disco (paso 8): el invitado con otro disco (la imagen europea, tercer argumento): el saludo debe rechazarlo (el caso
  positivo de la comprobacion del disco).
- menus (paso 8): el modo del menu Cooperativo (AC3_RED_MENUS: sin tarjeta y la mision en la partida nueva). El
  anfitrion elige la 5 y recorre los menus con su guion: los dos deben poner la 5 en la partida nueva y el juego
  arrancar el mismo la 5 (no la 1 forzada al arrancar), con las huellas iguales.
- tarjeta (paso 8): cargar una partida guardada en red. Primero se guarda una (partida nueva, menu de mision > SAVE,
  archivo 1) en una tarjeta nueva; despues el anfitrion, con esa tarjeta, la carga (load > archivo 1) y el invitado tiene
  una vacia: con AC3_RED_MENUS el anfitrion le envia la suya y las huellas deben ser iguales, sin tocar la del invitado.
  Control: el invitado se queda con la suya (AC3_RED_TARJETA=0): deben separarse.
- sonido (etapa 10): de quien es cada sonido. En red, el 1 dispara metralleta y misil y despues el 2
  (AC3_COOP_DBGVOCES): cada aparato anota las voces del otro (metralleta 0x11 en la voz 4, misil 0xe en la 12) como "no
  se oye aqui" y las suyas no; el motor (voces 0-2) nunca. Y sin red (AC3_COOP_LOCAL=2), el tono de fijado (0x32) del 1
  en la mision 5: no se oye en el aparato del 2. Las huellas, iguales (solo cambia la salida de audio).
- se_va (etapa 10): el invitado se cierra en el VBlank 3100 y el anfitrion sigue: su avion desaparece (el punto azul del
  radar, junto al 1, antes si y despues no) y la partida sigue.
- derribo (etapa 10): el mensaje de derribo (BULL'S-EYE) y su voz son de quien derriba. Mision 5, detras del enemigo
  2 con su vida a 50 (AC3_COOP_VIDA: un misil basta); derriba el 1 y, aparte, el 2; cada caso dibujando desde el 1 y
  desde el 2 (AC3_COOP_LOCAL igual a la vista): el rotulo (pixeles amarillos en la imagen 3300) solo en la vista de
  quien derriba, su voz (0x4c) solo sin silenciar en su aparato, y la misma partida desde las dos vistas.
- aviso (paso 8): el autopiloto (SELECT mantenido) del jugador 1 y, aparte, el del 2, sin red, dibujando desde el 1 y
  desde el 2: la barra azul "AUTOPILOT" (pixeles azules en la imagen 2850) solo en la vista de quien lo activa, y la
  misma partida desde las dos vistas. Antes del arreglo salia tambien en la vista del 2 cuando lo activaba el 1.

Uso:
    python3 tools/red.py <imagen_US> <salida> [red|control|vistas|anuncio|disco|menus|tarjeta|sonido|se_va|derribo|aviso ...]
                         [--eu <imagen_EU>]
"""
import os
import re
import socket
import struct
import subprocess
import sys
import time

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


def run(d, env, guion, frames=FRAMES, disc=None, shots=0):
    os.makedirs(d, exist_ok=True)
    e = dict(os.environ, **{k: v for k, v in env.items() if v is not None})
    for k in [k for k, v in env.items() if v is None]:   # None: sin la variable (AC3_WIDE vale con solo existir)
        e.pop(k, None)
    log = open(os.path.join(d, 'log.txt'), 'w')
    return subprocess.Popen([EXE, disc or DISC, '-', str(frames), d, str(shots), guion], env=e, stdout=log, stderr=subprocess.STDOUT,
                            cwd=ROOT)


def log_of(d):
    return open(os.path.join(d, 'log.txt'), errors='replace').read()


def pair(out, host_env, guest_env, g2=G2, g1=G1):
    """anfitrion e invitado conectados; devuelve los dos directorios"""
    h, g = os.path.join(out, 'anfitrion'), os.path.join(out, 'invitado')
    ph = run(h, dict(BASE, AC3_RED='anfitrion', AC3_RAMHASH=os.path.join(h, 'huella.bin'), **host_env), g1)
    pg = run(g, dict(BASE, AC3_RED='invitado', AC3_COOP_VER='2', AC3_GUION2=g2, AC3_MISSION=None,
                     AC3_RAMHASH=os.path.join(g, 'huella.bin'), **guest_env), G0)
    ph.wait()
    pg.wait()
    return h, g


def same_until_close(h, g):
    """las huellas de los dos, hasta que uno cierra (despues, si el que sigue es el anfitrion, quita el avion del 2)"""
    lost = [int(m) for d in (h, g) for m in re.findall(r'(?:cerro la conexion|conexion perdida al enviar) \(VBlank (\d+)\)',
                                                        log_of(d))]
    a = open(os.path.join(h, 'huella.bin'), 'rb').read()
    b = open(os.path.join(g, 'huella.bin'), 'rb').read()
    n = min(len(a), len(b)) // (35 * 8)
    if lost:
        n = min(n, min(lost) - 2)
    return n > 0 and a[:n * 35 * 8] == b[:n * 35 * 8]


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
    same = same_until_close(h, g)
    cmp, dif = net_line(h)
    fv, s, sr = flight(h), steps(h), steps(hr)
    adopted = 'el anfitrion eligio la mision 1' in log_of(g)
    pm = re.search(r'ping ([\d.]+) ms', log_of(h))
    ok = same and cmp > 0 and dif == 0 and 0 < fv < 3000 and sr - s == 150 and adopted
    print('red: mision del anfitrion en el invitado: %s; huellas de cada VBlank %s; protocolo: %d comparadas, %d distintas; '
          'vuelo desde el VBlank %d (START del 2); pasos de vuelo %d, sin pausa %d (la pausa del 2, quitada por el 1: %d menos);'
          ' ping %s ms -> %s' % ('si' if adopted else 'NO', 'iguales' if same else 'DISTINTAS', cmp, dif, fv, s, sr, sr - s,
                                 pm.group(1) if pm else '-', 'bien' if ok else 'MAL'), flush=True)
    return ok


def test_beacon(out):
    """el anuncio del anfitrion mientras espera: magic, version, puerto, NetInfo (opts, disc, mision, nombre, etiqueta)"""
    us = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    us.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    us.bind(('', 7071))
    us.settimeout(0.5)
    h = os.path.join(out, 'anuncio')
    p = run(h, dict(BASE, AC3_RED='anfitrion', AC3_RED_ESPERA='4', AC3_RED_NOMBRE='prueba'), G0, 10)
    got, t0 = [], time.time()
    while time.time() - t0 < 6 and p.poll() is None:
        try:
            data, src = us.recvfrom(256)
        except socket.timeout:
            continue
        if len(data) >= 12 + 92 and data[:4] == b'AC3L':
            ver, port, opts, disc, mission = struct.unpack_from('<IIIIi', data, 4)
            name = data[24:56].split(b'\0')[0].decode(errors='replace')
            got.append((time.time() - t0, ver, port, opts, disc, mission, name))
    p.wait()
    us.close()
    ok = len(got) >= 3 and all(g[1:3] == (2, 7071) and g[5] == 1 and g[6] == 'prueba' for g in got)
    gaps = [b[0] - a[0] for a, b in zip(got, got[1:])]
    print('anuncio: %d anuncios en ~4 s (cada %.1f s); version %s, puerto %s, disco %s, mision %s, nombre %s -> %s' % (
        len(got), sum(gaps) / len(gaps) if gaps else 0, *(('%d' % got[0][1], '%d' % got[0][2], '%08x' % got[0][4],
                                                             '%d' % got[0][5], got[0][6]) if got else ('-',) * 5),
        'bien' if ok else 'MAL'), flush=True)
    return ok


def test_disc(out, eu):
    if not eu:
        print('disco: falta --eu <imagen_EU>')
        return False
    d = os.path.join(out, 'disco')
    h, g = os.path.join(d, 'anfitrion'), os.path.join(d, 'invitado')
    ph = run(h, dict(BASE, AC3_RED='anfitrion', AC3_RED_ESPERA='20'), G0, 10)
    pg = run(g, dict(BASE, AC3_RED='invitado', AC3_RED_ESPERA='20', AC3_MISSION=None), G0, 10, disc=eu)
    ph.wait()
    pg.wait()
    msg = [l for l in log_of(g).split('\n') if 'otro disco' in l]
    ok = bool(msg) and pg.returncode != 0
    print('disco (invitado con la europea): %s -> %s' % (msg[0].replace('red: ', '') if msg else 'sin rechazo',
                                                         'bien' if ok else 'MAL'), flush=True)
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


def test_menus(out):
    d = os.path.join(out, 'menus')
    h, g = os.path.join(d, 'anfitrion'), os.path.join(d, 'invitado')
    ph = run(h, dict(BASE, AC3_RED='anfitrion', AC3_MISSION='5', AC3_RED_MENUS='1', AC3_RAMHASH=os.path.join(h, 'huella.bin')),
             G0, 3000)
    pg = run(g, dict(BASE, AC3_RED='invitado', AC3_MISSION=None, AC3_RED_MENUS='1', AC3_COOP_VER='2',
                     AC3_RAMHASH=os.path.join(g, 'huella.bin')), G0, 3000)
    ph.wait()
    pg.wait()
    same = same_until_close(h, g)
    ng = [re.search(r'partida nueva en el VBlank (\d+): la mision (\d+)', log_of(x)) for x in (h, g)]
    st = [re.search(r'el juego arranca la (\d+)', log_of(x)) for x in (h, g)]
    ok = same and all(ng) and all(m.group(2) == '5' for m in ng) and all(st) and all(m.group(1) == '5' for m in st)
    print('menus: partida nueva %s; el juego arranca la %s; %s -> %s' % (
        ' y '.join('VBlank %s mision %s' % (m.group(1), m.group(2)) if m else '-' for m in ng),
        '/'.join(m.group(1) if m else '-' for m in st), 'huellas iguales' if same else 'DISTINTAS', 'bien' if ok else 'MAL'),
        flush=True)
    return ok


SAVE = ('300:start:8,500:cruz:6,700:cruz:6,1100:derecha:6,1250:derecha:6,1400:cruz:6,1600:cruz:6,1800:cruz:6,'
        '2000:cruz:6,2150:arriba:6,2250:cruz:6,2600:cruz:6')   # partida nueva, menu de mision > SAVE, archivo 1, si
LOAD = '300:start:8,500:abajo:6,600:cruz:6,800:cruz:6,1200:cruz:6,1400:cruz:6,1600:cruz:6,1800:cruz:6,2000:cruz:6'


def card_files(card):
    d = open(card, 'rb').read()
    return [d[b * 128 + 10:b * 128 + 30].split(b'\0')[0].decode() for b in range(1, 16)
            if int.from_bytes(d[b * 128:b * 128 + 4], 'little') == 0x51]


def test_card(out):
    d = os.path.join(out, 'tarjeta')
    os.makedirs(d, exist_ok=True)
    saved = os.path.join(d, 'con_partida.mcd')
    for f in (saved, os.path.join(d, 'vacia.mcd')):
        if os.path.exists(f):
            os.remove(f)
    p = run(os.path.join(d, 'guardar'), dict(AC3_PAD_ID='73', AC3_CARD=saved), SAVE, 3000)   # (sin archivo: una nueva)
    p.wait()
    files = card_files(saved)
    ok = bool(files)
    for control in (False, True):
        k = os.path.join(d, 'control' if control else 'bien')
        h, g = os.path.join(k, 'anfitrion'), os.path.join(k, 'invitado')
        os.makedirs(k, exist_ok=True)
        hc, gc = os.path.join(k, 'anfitrion.mcd'), os.path.join(k, 'invitado.mcd')
        open(hc, 'wb').write(open(saved, 'rb').read())
        if os.path.exists(gc):
            os.remove(gc)
        ph = run(h, dict(BASE, AC3_RED='anfitrion', AC3_MISSION=None, AC3_RED_MENUS='1', AC3_CARD=hc,
                         AC3_RAMHASH=os.path.join(h, 'huella.bin')), LOAD, 3000)
        genv = dict(BASE, AC3_RED='invitado', AC3_MISSION=None, AC3_RED_MENUS='1', AC3_COOP_VER='2', AC3_CARD=gc,
                    AC3_RAMHASH=os.path.join(g, 'huella.bin'))
        if control:
            genv['AC3_RED_TARJETA'] = '0'
        time.sleep(0.5)
        pg = run(g, genv, '', 3000)
        ph.wait()
        pg.wait()
        same = same_until_close(h, g)
        cmp, dif = net_line(h)
        untouched = not card_files(gc)
        good = (not same and dif > 0) if control else (same and dif == 0 and untouched)
        ok = ok and good
        print('tarjeta%s: guardada %s; el anfitrion la carga, el invitado con una vacia%s: %d comparadas, %d distintas; '
              'tarjeta del invitado %s -> %s' % (' (control)' if control else '', ','.join(files) or 'NADA',
                                                 ' que se queda' if control else ' (recibe la del anfitrion)', cmp, dif,
                                                 'intacta' if untouched else 'CAMBIADA', 'bien' if good else 'MAL'),
              flush=True)
    return ok


def voices(d):
    """(voz, dueno, silenciada) de cada encendido anotado"""
    return [(int(v), w, bool(m)) for v, w, m in
            re.findall(r'voz (\d+) del (jugador 1|jugador 2|comun)( \(no se oye aqui\))?', log_of(d))]


def test_sound(out):
    d = os.path.join(out, 'sonido')
    h, g = os.path.join(d, 'anfitrion'), os.path.join(d, 'invitado')
    ph = run(h, dict(BASE, AC3_RED='anfitrion', AC3_COOP_DBGVOCES='1', AC3_RAMHASH=os.path.join(h, 'huella.bin')),
             G0 + ',2700:start:6,2850:cruz:30,2950:circulo:4', 3300)
    pg = run(g, dict(BASE, AC3_RED='invitado', AC3_MISSION=None, AC3_COOP_VER='2', AC3_COOP_DBGVOCES='1',
                     AC3_GUION2='3050:cruz:30,3150:circulo:4', AC3_RAMHASH=os.path.join(g, 'huella.bin')), G0, 3300)
    ph.wait()
    pg.wait()
    ok = same_until_close(h, g)
    for d2, me, other in ((h, 'jugador 1', 'jugador 2'), (g, 'jugador 2', 'jugador 1')):
        vs = voices(d2)
        mine_ok = any(v in (4, 12) and w == me and not m for v, w, m in vs)
        other_muted = [v for v, w, m in vs if w == other and m]
        other_heard = [v for v, w, m in vs if w == other and not m and v >= 3]
        engine_muted = [v for v, w, m in vs if v < 3 and m]
        good = mine_ok and 4 in other_muted and 12 in other_muted and not other_heard and not engine_muted
        ok = ok and good
        print('sonido: el aparato del %s: los suyos se oyen %s; del %s sin oir: voces %s; del %s oidas: %s; motor silenciado: %s -> %s'
              % (me, 'si' if mine_ok else 'NO', other, sorted(set(other_muted)), other, sorted(set(other_heard)) or 'ninguna',
                 'si' if engine_muted else 'no', 'bien' if good else 'MAL'), flush=True)
    # el tono de fijado del 1 (mision 5, detras del enemigo 2: coop_vista misil), en el aparato del 2
    k = os.path.join(d, 'fijado')
    p = run(k, dict(AC3_PAD_ID='73', AC3_MISSION='5', AC3_COOP='1', AC3_PAD2_ID='73', AC3_COOP_DESDE='3150',
                    AC3_COOP_DIST='600', AC3_COOP_TRAS1='2', AC3_COOP_LOCAL='2', AC3_COOP_DBGVOCES='1'),
            G0 + ',3220:circulo:4', 3400)
    p.wait()
    lock = re.findall(r'peticion: sonido 32 en la voz (\d+) \((\w+ ?\d?)\)', log_of(k))
    vs = voices(k)
    lv = {int(v) for v, w in lock}
    good = bool(lock) and all(w == 'jugador 1' for v, w in lock) and all(m for v, w, m in vs if v in lv and w == 'jugador 1')
    ok = ok and good
    print('sonido: tono de fijado (0x32) del 1 en el aparato del 2: %d peticiones del %s, sin oir: %s -> %s' % (
        len(lock), ','.join(sorted({w for v, w in lock})) or '-', 'si' if good else 'NO', 'bien' if good else 'MAL'), flush=True)
    return ok


def radar_blue(ppm):
    """pixeles del punto azul del otro jugador en el radar (49, 156-206, 255)"""
    data = open(ppm, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    n = 0
    for y in range(8, 78):
        for x in range(8, 75):
            r, g, b = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
            n += b > 200 and r < 100 and g > 120
    return n


def test_leave(out):
    d = os.path.join(out, 'se_va')
    h, g = os.path.join(d, 'anfitrion'), os.path.join(d, 'invitado')
    ph = run(h, dict(BASE, AC3_RED='anfitrion', AC3_COOP_POS='0,400'), G0 + ',2700:start:6', 3400, shots=50)
    pg = run(g, dict(BASE, AC3_RED='invitado', AC3_MISSION=None, AC3_COOP_VER='2', AC3_COOP_POS='0,400'), G0, 3100)
    ph.wait()
    pg.wait()
    m = re.search(r'el jugador 2 se fue en el VBlank (\d+)', log_of(h))
    before, after = radar_blue(os.path.join(h, 'f03000.ppm')), radar_blue(os.path.join(h, 'f03300.ppm'))
    good = bool(m) and before > 0 and after == 0
    print('se_va: el invitado cierra; el anfitrion lo quita en el VBlank %s; punto azul en el radar: %d px antes, %d despues -> %s'
          % (m.group(1) if m else '-', before, after, 'bien' if good else 'MAL'), flush=True)
    return good


def yellow_text(ppm):
    """pixeles del rotulo de derribo (amarillo) en la franja de arriba"""
    data = open(ppm, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    n = 0
    for y in range(60, 110):
        for x in range(w):
            r, g, b = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
            n += r > 200 and g > 150 and b < 90
    return n


def test_kill(out):
    ok = True
    for who in (1, 2):
        ps = []
        for v in (1, 2):
            d = os.path.join(out, 'derribo', 'j%d_ver%d' % (who, v))
            env = dict(AC3_PAD_ID='73', AC3_MISSION='5', AC3_COOP='1', AC3_PAD2_ID='73', AC3_COOP_DESDE='3150',
                       AC3_COOP_DIST='600', AC3_COOP_VIDA='2,50', AC3_COOP_VER=str(v), AC3_COOP_LOCAL=str(v),
                       AC3_COOP_DBGVOCES='1', AC3_WIDE='1', AC3_COMO_APK='1', AC3_SPU='1',
                       AC3_RAMHASH=os.path.join(d, 'huella.bin'))
            if who == 1:
                env['AC3_COOP_TRAS1'] = '2'
                g1 = G0 + ',3220:circulo:4'
            else:
                env.update(AC3_COOP_TRAS='2', AC3_GUION2='3220:circulo:4')
                g1 = G0
            ps.append((v, d, run(d, env, g1, 3400, shots=20)))
        text, voice = {}, {}
        for v, d, p in ps:
            p.wait()
            text[v] = yellow_text(os.path.join(d, 'f03300.ppm'))
            m = re.findall(r'peticion: sonido 4c en la voz (\d+) \((\w+ \d)\)', log_of(d))
            vs = [(n, w, mu) for n, w, mu in voices(d) if m and n == int(m[0][0]) and w == m[0][1]]
            voice[v] = (m[0][1] if m else '-', vs[0][2] if vs else None)
        same = open(os.path.join(ps[0][1], 'huella.bin'), 'rb').read() == open(os.path.join(ps[1][1], 'huella.bin'), 'rb').read()
        good = (same and text[who] > 100 and text[3 - who] == 0 and voice[who] == ('jugador %d' % who, False)
                and voice[3 - who] == ('jugador %d' % who, True))
        ok = ok and good
        print('derribo: derriba el %d: rotulo en la vista del 1 %d px, en la del 2 %d px; su voz en el aparato del 1 %s, en el del 2 '
              '%s; %s -> %s' % (who, text[1], text[2], 'sin oir' if voice[1][1] else 'se oye', 'sin oir' if voice[2][1] else 'se oye',
                               'la misma partida' if same else 'DISTINTAS', 'bien' if good else 'MAL'), flush=True)
    return ok


def blue_bar(ppm):
    """pixeles azules donde sale la barra "AUTOPILOT" (911 con ella, 0 sin ella; sin Pillow: el PPM a mano)"""
    data = open(ppm, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    n = 0
    for y in range(70, 95):
        for x in range(80, 240):
            r, g, b = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
            n += b > 150 and r < 120 and g < 140
    return n


def test_hud_message(out, control=False):
    """control=True: AC3_COOP_AVISO=0 (sin la copia del aviso del 2): la barra del 1 debe salir tambien en la vista del 2"""
    ok = True
    for who in ((1,) if control else (1, 2)):
        ps = []
        for v in (1, 2):
            d = os.path.join(out, 'aviso_control' if control else 'aviso', 'j%d_ver%d' % (who, v))
            g1 = G0 + ',2700:start:6' + (',2800:select:120' if who == 1 else '')
            env = dict(AC3_PAD_ID='73', AC3_MISSION='1', AC3_COOP='1', AC3_PAD2_ID='73', AC3_WIDE='1', AC3_COMO_APK='1',
                       AC3_SPU='1', AC3_COOP_VER=str(v), AC3_COOP_POS='300,-200',
                       AC3_GUION2='2800:select:120' if who == 2 else '', AC3_RAMHASH=os.path.join(d, 'huella.bin'))
            if control:
                env['AC3_COOP_AVISO'] = '0'
            ps.append((v, d, run(d, env, g1, 3000, shots=50)))
        bars = {}
        for v, d, p in ps:
            p.wait()
            bars[v] = blue_bar(os.path.join(d, 'f02850.ppm'))
        same = open(os.path.join(ps[0][1], 'huella.bin'), 'rb').read() == open(os.path.join(ps[1][1], 'huella.bin'), 'rb').read()
        good = same and (bars[who] > 500) and ((bars[3 - who] > 500) if control else (bars[3 - who] == 0))
        ok = ok and good
        print('aviso%s: autopiloto del jugador %d: barra en la vista del 1 %d px, en la del 2 %d px; %s -> %s' % (
            ' (control, sin el arreglo: debe salir en las dos)' if control else '', who, bars[1], bars[2],
            'la misma partida' if same else 'DISTINTAS', 'bien' if good else 'MAL'), flush=True)
    return ok


def main():
    global DISC
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    args = sys.argv[1:]
    eu = None
    if '--eu' in args:
        i = args.index('--eu')
        eu = args[i + 1]
        del args[i:i + 2]
    DISC, out = args[0], args[1]
    what = args[2:] or ['red', 'control', 'vistas', 'anuncio', 'menus', 'tarjeta', 'sonido', 'se_va', 'derribo', 'aviso'] + (
        ['disco'] if eu else [])
    ok = True
    if 'red' in what:
        ok = test_net(out) and ok
    if 'control' in what:
        ok = test_control(out) and ok
    if 'vistas' in what:
        ok = test_views(out) and ok
    if 'anuncio' in what:
        ok = test_beacon(out) and ok
    if 'disco' in what:
        ok = test_disc(out, eu) and ok
    if 'menus' in what:
        ok = test_menus(out) and ok
    if 'tarjeta' in what:
        ok = test_card(out) and ok
    if 'sonido' in what:
        ok = test_sound(out) and ok
    if 'se_va' in what:
        ok = test_leave(out) and ok
    if 'derribo' in what:
        ok = test_kill(out) and ok
    if 'aviso' in what:
        ok = test_hud_message(out) and ok
        ok = test_hud_message(out, control=True) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
