#!/usr/bin/env python3
"""coop_vista — la vista del jugador 2 (etapa 9, paso 5).

Con el binario libre (make libre-build) y AC3_COOP=1 (psx/recomp/ac3_coop.c), tres pruebas:

- misil: mision 5, el jugador 2 aparece en el VBlank 3150 a 600 unidades detras del enemigo 2 (AC3_COOP_TRAS,
  AC3_COOP_DIST), girado 0, 4 y 6 grados, y lanza un misil en el 3220. Con su vista en la simulacion (su paso de camara
  y la logica de su HUD) lo fija y el misil sale con blanco; sin ella (AC3_COOP_VISTA=0), sin blanco y recto. Control
  positivo: lo mismo con el jugador 1 (AC3_COOP_TRAS1). Se anota el blanco del lanzamiento y la vida del enemigo en el
  VBlank 3600.
- huella: mision 0 con la SPU, dibujando desde el jugador 1 y desde el 2 (AC3_COOP_VER=2): la huella de la RAM de cada
  VBlank (AC3_RAMHASH) debe ser la misma (lockstep: cada aparato dibuja a su jugador y la partida es una).
- autoprueba: el mismo redibujo, pero como el jugador 1 (AC3_COOP_AUTOPRUEBA): debe dar el original pixel a pixel.
  Conocido: las 16 primeras imagenes del vuelo (la transicion de ruido se dibuja fuera de la lista de la mision).

Uso:
    python3 tools/coop_vista.py <imagen_US> <salida> [misil|huella|autoprueba ...]
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import misiones  # noqa: E402

EXE = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
# el guion de tools/misiones.py hasta que empieza el vuelo (sin los giros ni disparos del jugador 1)
G0 = ','.join(x for x in misiones.GUION.split(',') if int(x.split(':')[0]) <= 2300)
GUION2 = '2700:lder:120,2900:laba:60,3100:lizq:120,3300:larr:60,3500:lder5:200,3000:cruz:60,3400:circulo:4'


def start(disc, d, frames, env, guion=G0):
    os.makedirs(d, exist_ok=True)
    e = dict(os.environ, AC3_PAD_ID='73', AC3_COOP='1', AC3_PAD2_ID='73', **env)
    log = open(os.path.join(d, 'log.txt'), 'w')
    return subprocess.Popen([EXE, disc, '-', str(frames), d, '0', guion], env=e, stdout=log, stderr=subprocess.STDOUT,
                            cwd=ROOT)


def read_log(d):
    return open(os.path.join(d, 'log.txt'), errors='replace').read()


def missile(disc, out):
    runs = {}
    for who in (1, 2):
        for vista in ((1, 0) if who == 2 else (1,)):
            for giro in (0, 4, 6):
                d = os.path.join(out, 'misil', 'j%d_v%d_g%d' % (who, vista, giro))
                env = dict(AC3_MISSION='5', AC3_COOP_DESDE='3150', AC3_COOP_DIST='600', AC3_COOP_MISIL='1',
                           AC3_COOP_VISTA=str(vista), AC3_DUMPMEM='3600,80000000,200000,%s' % os.path.join(d, 'ram.bin'))
                if who == 1:
                    env.update(AC3_COOP_TRAS1='2', AC3_COOP_GIRO1=str(giro))
                    guion = G0 + ',3220:circulo:4'
                else:
                    env.update(AC3_COOP_TRAS='2', AC3_COOP_GIRO=str(giro), AC3_GUION2='3220:circulo:4')
                    guion = G0
                runs[(who, vista, giro)] = (d, start(disc, d, 3601, env, guion))
    for (who, vista, giro), (d, p) in sorted(runs.items()):
        p.wait()
        ram = open(os.path.join(d, 'ram.bin'), 'rb').read()
        w = lambda a: struct.unpack_from('<I', ram, a & 0x1FFFFF)[0]
        e2 = w(w(0x800BB824) + 8)
        hp = struct.unpack_from('<h', ram, (e2 + 0x4E) & 0x1FFFFF)[0]
        shots = [l.split('con blanco ')[1] for l in read_log(d).split('\n') if 'misil del jugador %d' % who in l]
        print('jugador %d%s, girado %d: misil con blanco %s; vida del enemigo 2: 140 -> %d' % (
            who, '' if who == 1 else (' con su vista' if vista else ' sin su vista'), giro, ','.join(shots) or '-', hp),
            flush=True)


def hashes(disc, out):
    ps = []
    for v in (1, 2):
        d = os.path.join(out, 'huella', 'ver%d' % v)
        os.makedirs(d, exist_ok=True)
        ps.append((d, start(disc, d, 4001, dict(AC3_MISSION='0', AC3_SPU='1', AC3_COOP_VER=str(v), AC3_GUION2=GUION2,
                                                 AC3_RAMHASH=os.path.join(d, 'huella.bin')))))
    for d, p in ps:
        p.wait()
    h = [open(os.path.join(d, 'huella.bin'), 'rb').read() for d, _ in ps]
    n = len(h[0]) // (35 * 8)
    views = [l for l in read_log(ps[1][0]).split('\n') if 'imagenes desde' in l]
    same = h[0] == h[1]
    print('huella: %d VBlank, desde el jugador 1 y desde el 2 (%s): %s' % (
        n, views[-1].replace('coop: ', '') if views else '-', 'la misma partida' if same else 'DISTINTAS'), flush=True)
    return same


def selftest(disc, out):
    ps = []
    for m in (0, 5, 19, 37):
        d = os.path.join(out, 'autoprueba', str(m))
        ps.append((m, d, start(disc, d, 3601, dict(AC3_MISSION=str(m), AC3_COOP_AUTOPRUEBA='1', AC3_GUION2=GUION2))))
    for m, d, p in ps:
        p.wait()
        res = [l for l in read_log(d).split('\n') if 'autoprueba:' in l]
        print('autoprueba, mision %2d: %s' % (m, res[0].replace('coop: autoprueba: ', '') if res else 'sin datos'), flush=True)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    disc, out = sys.argv[1], sys.argv[2]
    what = sys.argv[3:] or ['misil', 'huella', 'autoprueba']
    ok = True
    if 'misil' in what:
        missile(disc, out)
    if 'huella' in what:
        ok = hashes(disc, out) and ok
    if 'autoprueba' in what:
        selftest(disc, out)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
