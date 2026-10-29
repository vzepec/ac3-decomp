#!/usr/bin/env python3
"""red_repro — repite en el PC una partida en red grabada por el APK (etapa 10).

El APK de desarrollo, en cada partida del menu Cooperativo, deja en su carpeta (adb pull
/sdcard/Android/data/com.vzepec.ac3game.dev/files/red_partida_*): la mision y las opciones (red_partida_info.txt), la
tarjeta con la que se juega (red_partida_tarjeta.mcd), los mandos de los dos puertos de cada VBlank
(red_partida_mandos.bin, 16 bytes: v y dos mandos de 6) y la huella de 64 bits de cada 60 VBlank que se compara por la
red (red_partida_huellas.bin, 12 bytes: v y la huella).

Con el binario libre (AC3_REPRO, psx/recomp/native_main.c) se repite dos veces: dibujando desde el jugador 1 (como
quien crea) y desde el 2 (como quien se une: AC3_COOP_VER=2 y AC3_COOP_LOCAL=2). Se compara:
- la huella de cada aparato grabado con la de la repeticion (la primera distinta dice que aparato se separo y cuando;
  si coinciden hasta el final, la repeticion es fiel);
- las dos repeticiones entre si, VBlank a VBlank (AC3_RAMHASH): si se separan, el fallo esta en lo que cambia al dibujar
  desde el 2, y se dice el primer VBlank y que bloques de la RAM.

Uso:
    python3 tools/red_repro.py <imagen_US> <salida> <carpeta_grabada> [<carpeta_del_otro>]
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
REC = 35 * 8


def info(d):
    out = {}
    for line in open(os.path.join(d, 'red_partida_info.txt')):
        k, _, v = line.strip().partition('=')
        if v:
            out[k] = int(v)
    return out


def hashes60(path):
    data = open(path, 'rb').read() if os.path.exists(path) else b''
    return {struct.unpack_from('<i', data, i)[0]: struct.unpack_from('<Q', data, i + 4)[0] for i in range(0, len(data) - 11, 12)}


def first_diff(a, b):
    for v in sorted(set(a) & set(b)):
        if a[v] != b[v]:
            return v
    return None


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    disc, out, recs = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    src = recs[0]
    inf = info(src)
    n = os.path.getsize(os.path.join(src, 'red_partida_mandos.bin')) // 16
    print('grabacion %s: mision %d, opciones %x, %d VBlank' % (src, inf.get('mision', -1), inf.get('opciones', 0), n), flush=True)
    ps = []
    for v in (1, 2):
        d = os.path.join(out, 'vista%d' % v)
        os.makedirs(d, exist_ok=True)
        env = dict(os.environ, AC3_PAD_ID='73', AC3_COOP='1', AC3_PAD2_ID='73', AC3_REPRO=os.path.abspath(src),
                   AC3_REPRO_HUELLAS=os.path.join(d, 'huellas.bin'), AC3_RAMHASH=os.path.join(d, 'ram.bin'),
                   AC3_COOP_VER=str(v), AC3_COOP_LOCAL=str(v))
        if inf.get('opciones', 0) & 1:
            env['AC3_WIDE'] = '1'
        else:
            env.pop('AC3_WIDE', None)
        log = open(os.path.join(d, 'log.txt'), 'w')
        ps.append((v, d, subprocess.Popen([EXE, disc, '-', str(n), d, '0', ''], env=env, stdout=log,
                                          stderr=subprocess.STDOUT, cwd=ROOT)))
    for _, _, p in ps:
        p.wait()
    rep = {v: hashes60(os.path.join(d, 'huellas.bin')) for v, d, _ in ps}
    ok = True
    for r in recs:
        dev = hashes60(os.path.join(r, 'red_partida_huellas.bin'))
        role = 'quien crea' if info(r).get('anfitrion') else 'quien se une'
        v = 1 if info(r).get('anfitrion') else 2
        fd = first_diff(dev, rep[v])
        common = len(set(dev) & set(rep[v]))
        print('%s (%s): %d huellas, %d comparadas con la repeticion desde el %d; %s' % (
            r, role, len(dev), common, v, 'iguales' if fd is None else 'la primera distinta en el VBlank %d' % fd), flush=True)
        ok = ok and fd is None and common > 0
    a = open(os.path.join(ps[0][1], 'ram.bin'), 'rb').read()
    b = open(os.path.join(ps[1][1], 'ram.bin'), 'rb').read()
    m = min(len(a), len(b)) // REC
    for i in range(m):
        if a[i * REC:(i + 1) * REC] != b[i * REC:(i + 1) * REC]:
            x = struct.unpack_from('<35Q', a, i * REC)
            y = struct.unpack_from('<35Q', b, i * REC)
            blocks = ['%d (%05x)' % (k - 1, (k - 1) * 0x10000) if k <= 32 else ('scratchpad' if k == 33 else 'rand')
                      for k in range(1, 35) if x[k] != y[k]]
            print('repeticiones desde el 1 y desde el 2: distintas desde el VBlank %d; bloques: %s' % (x[0], ', '.join(blocks)),
                  flush=True)
            ok = False
            break
    else:
        print('repeticiones desde el 1 y desde el 2: la misma partida en los %d VBlank' % m, flush=True)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
