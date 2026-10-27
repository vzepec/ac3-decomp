#!/usr/bin/env python3
"""coop — el segundo avion en las misiones (etapa 9, paso 3).

Con el binario libre (make libre-build) y AC3_COOP=1: en cada paso de vuelo, al acabar el jugador 1,
el juego vuelve a ejecutar el paso del jugador con el contexto del jugador 2 (la entidad de la
ranura 1, sus propios bloques y el mando del puerto 2; psx/recomp/ac3_coop.c). Por mision:
- dos ejecuciones con el mismo guion del jugador 1 (tools/misiones.py), una sin y otra con el
  jugador 2 (guion GUION2: gira, sube, baja), volcando las dos entidades en el VBlank V;
- se anota si hay fallos del codigo del juego (rc_fault), si el jugador 2 sigue vivo, cuanto se ha
  movido y si su rumbo cambio (obedece a su mando), y si el jugador 1 sale igual que sin el 2.
Las misiones con una introduccion larga (1, 12, 16, 29, 32) aun no vuelan en el VBlank 3600: se anotan
aparte (con --vblank 8000 aparece en todas).

Uso:
    python3 tools/coop.py <imagen_US> <salida> [misiones] [procesos] [--vblank V]
    p. ej. python3 tools/coop.py "Rom/...(USA).bin" psx/build/coop 0-40 4
"""
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import misiones  # noqa: E402

GUION2 = '2700:lder:120,2900:laba:60,3100:lizq:120,3300:larr:60,3500:lder5:200'
E0 = 0x80134F68 - 0x80134BAC   # entidad 0 respecto a g_render (la 1 va 0x58 despues)


def run(exe, disc, d, m, vb, coop):
    os.makedirs(d, exist_ok=True)
    env = dict(os.environ, AC3_PAD_ID='73', AC3_MISSION=str(m),
               AC3_DUMPMEM='%d,80000000,200000,%s' % (vb, os.path.join(d, 'ram.bin')))
    if coop:
        env.update(AC3_COOP='1', AC3_PAD2_ID='73', AC3_GUION2=GUION2, AC3_COOP_LOG='100')
    with open(os.path.join(d, 'log.txt'), 'w') as log:
        p = subprocess.Popen([exe, disc, '-', str(vb + 1), d, '0', misiones.GUION], env=env, stdout=log,
                             stderr=subprocess.STDOUT, cwd=ROOT)
        return p


def entity(ram, e):
    w = lambda a: struct.unpack_from('<I', ram, a & 0x1FFFFF)[0]
    i = lambda a: struct.unpack_from('<i', ram, a & 0x1FFFFF)[0]
    h = lambda a: struct.unpack_from('<h', ram, a & 0x1FFFFF)[0]
    return {'banderas': w(e), 'pos': [i(e + o) >> 12 for o in (8, 12, 16)], 'ry': h(e + 0x1A), 'vida': h(e + 0x4E),
            'bytes': ram[e & 0x1FFFFF:(e & 0x1FFFFF) + 0x58]}


def main():
    opts, args, i = sys.argv[1:], [], 0
    vb = 3600
    while i < len(opts):
        if opts[i] == '--vblank':
            vb = int(opts[i + 1])
            i += 2
            continue
        args.append(opts[i])
        i += 1
    if len(args) < 2:
        sys.exit(__doc__)
    disc, out = args[0], args[1]
    ms = misiones.parse(args[2]) if len(args) > 2 else list(range(41))
    nproc = int(args[3]) if len(args) > 3 else 2
    exe = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
    jobs = [(m, c) for m in ms for c in (0, 1)]
    running, bad = [], 0
    results = {}
    while jobs or running:
        while jobs and len(running) < nproc:
            m, c = jobs.pop(0)
            d = os.path.join(out, str(m), 'con' if c else 'sin')
            running.append((m, c, d, run(exe, disc, d, m, vb, c)))
        m, c, d, p = running.pop(0)
        code = p.wait()
        log = open(os.path.join(d, 'log.txt'), errors='replace').read()
        results[(m, c)] = (code, log.count('recomp: fallo'), d, log)
        if (m, 0) in results and (m, 1) in results:
            c0, f0, d0, _ = results[(m, 0)]
            c1, f1, d1, log1 = results[(m, 1)]
            try:
                r0, r1 = open(os.path.join(d0, 'ram.bin'), 'rb').read(), open(os.path.join(d1, 'ram.bin'), 'rb').read()
            except OSError:
                print('mision %2d: sin volcado (salida %s/%s)' % (m, c0, c1))
                bad += 1
                continue
            gr = struct.unpack_from('<I', r1, 0x800BB81C & 0x1FFFFF)[0]
            p1s, p1c, p2 = entity(r0, gr + E0), entity(r1, gr + E0), entity(r1, gr + E0 + 0x58)
            act = re.search(r'entidad 1 \(\w+\) activa en el VBlank (\d+)', log1)
            steps = re.search(r'coop: (\d+) pasos', log1)
            p2log = [l for l in log1.split('\n') if 'jugador 2 pos' in l]
            ry0 = int(p2log[0].split(' ang ')[1].split()[1]) if p2log else 0
            moved = sum(abs(a - b) for a, b in zip(p2['pos'], p1c['pos']))
            ok = c1 == 0 and f1 == 0
            bad += not ok
            if ok and act is None:   # la 1, 12, 16, 29 y 32: introduccion larga, aun sin vuelo en el VBlank V
                print('mision %2d: bien; aun sin vuelo en el VBlank %d (el jugador 2 aparece al empezar el vuelo); jugador 1 %s que sin el 2'
                      % (m, vb, 'igual' if p1s['bytes'] == p1c['bytes'] else 'distinto'), flush=True)
                continue
            print('mision %2d: %s; jugador 2 desde el VBlank %s, %s pasos, vida %d, rumbo %d -> %d, a %d del 1; '
                  'jugador 1 %s que sin el 2' % (
                      m, 'bien' if ok else 'MAL (salida %s, %d fallos)' % (c1, f1), act.group(1) if act else '-',
                      steps.group(1) if steps else '0', p2['vida'], ry0, p2['ry'], moved,
                      'igual' if p1s['bytes'] == p1c['bytes'] else 'distinto'), flush=True)
    print('%d misiones: %d bien' % (len(ms), len(ms) - bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
