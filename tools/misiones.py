#!/usr/bin/env python3
"""misiones — carga una a una las 41 misiones del juego y comprueba que funcionan (etapa 6, paso 5).

Usa el binario libre (make libre: psx/build/ac3_libre, el mismo codigo que el APK) con
AC3_MISSION=N: al entrar en la funcion que arranca la mision (EU 0x800355F0, US 0x800357B8) el
interprete escribe N en la mision actual del estado de la partida (g_state + 0x390, short), y el
juego carga mission_records[N] (registros 6..46 del BPB). Solo se fuerza la primera vez: las
misiones que encadenan otra (la 16, el lanzamiento, sigue en la 17) siguen su curso.
Por mision se anota: que mision arranca (y si el juego encadena otra), fallos del codigo del
juego (rc_fault: memoria, instruccion...), si el proceso termina bien, y capturas cada 300
fotogramas. Guion: hasta la mision con los botones del guion de la US y luego vuela a ciegas con
la palanca izquierda y dispara misiles (el avion puede estrellarse: eso es el juego).
Con Pillow, ademas, una hoja de contactos por cada 11 misiones (hojaN.png). Cada mision deja en
<salida>/<N>/cobertura.txt las funciones en las que entro el interprete (tools/cobertura.py).

Uso:
    python3 tools/misiones.py <imagen_del_disco> <salida> [misiones] [procesos] [--eu]
    p. ej. python3 tools/misiones.py "Rom/.../...(USA).bin" psx/build/misiones 0-40 2
Devuelve 0 si todas terminan sin fallos.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUION = ('300:start:8,500:cruz:6,700:cruz:6,900:cruz:6,1100:cruz:6,1300:cruz:6,1500:cruz:6,1700:cruz:6,'
         '1900:cruz:6,2100:cruz:6,2300:cruz:6,2900:lizq:60,3050:lder:60,3200:larr:40,3300:circulo:6,'
         '3400:laba:40,3500:circulo:6,3600:rder:30,3700:lizq+larr:60,3850:circulo:6')
FRAMES = 4000
SHEET = [2700, 3000, 3300, 3600, 3900]
# europea (--eu): menu de idioma; la mision arranca hacia el fotograma 3500 (50 Hz)
GUION_EU = ('320:cruz:6,1100:start:8,1400:cruz:6,1600:cruz:6,1800:cruz:6,2000:cruz:6,2200:cruz:6,2400:cruz:6,'
            '2600:cruz:6,2800:cruz:6,3000:cruz:6,3200:cruz:6,3400:cruz:6,4000:lizq:60,4150:lder:60,4300:larr:40,'
            '4400:circulo:6,4500:laba:40,4600:circulo:6,4700:rder:30,4800:lizq+larr:60,4950:circulo:6')
FRAMES_EU = 5100
SHEET_EU = [3900, 4200, 4500, 4800, 5100]


def parse(spec):
    out = []
    for part in spec.split(','):
        a, _, b = part.partition('-')
        out += list(range(int(a), int(b or a) + 1))
    return out


def sheets(out, ms, frames=None):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    frames = frames or SHEET
    W, H = 160, 128
    for k in range(0, len(ms), 11):
        group = ms[k:k + 11]
        img = Image.new('RGB', (30 + W * len(frames), H * len(group)), 'black')
        d = ImageDraw.Draw(img)
        for r, m in enumerate(group):
            d.text((4, r * H + 4), str(m), fill='yellow')
            for c, f in enumerate(frames):
                p = os.path.join(out, str(m), 'f%05d.ppm' % f)
                if os.path.exists(p):
                    img.paste(Image.open(p).convert('RGB').resize((W, H)), (30 + c * W, r * H))
        img.save(os.path.join(out, 'hoja%d.png' % (k // 11 + 1)))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    argv = [a for a in sys.argv if a != '--eu']
    eu = '--eu' in sys.argv
    guion, frames, sheet = (GUION_EU, FRAMES_EU, SHEET_EU) if eu else (GUION, FRAMES, SHEET)
    disc, out = argv[1], argv[2]
    ms = parse(argv[3]) if len(argv) > 3 else list(range(41))
    nproc = int(argv[4]) if len(argv) > 4 else 2
    exe = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
    if not os.path.exists(exe):
        sys.exit('falta psx/build/ac3_libre: make libre')
    env = dict(os.environ, AC3_PAD_ID='73')
    running, results = [], {}
    todo = list(ms)
    while todo or running:
        while todo and len(running) < nproc:
            m = todo.pop(0)
            d = os.path.join(out, str(m))
            os.makedirs(d, exist_ok=True)
            log = open(os.path.join(d, 'log.txt'), 'w')
            p = subprocess.Popen([exe, disc, '-', str(frames), d, '300', guion],
                                 env=dict(env, AC3_MISSION=str(m), AC3_COVERAGE=os.path.join(d, 'cobertura.txt')),
                                 stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
            running.append((m, p, log))
        m, p, log = running.pop(0)
        try:
            code = p.wait(timeout=900)
        except subprocess.TimeoutExpired:
            p.kill()
            code = 'colgado'
        log.close()
        txt = open(os.path.join(out, str(m), 'log.txt'), errors='replace').read()
        starts = re.findall(r'^mision: el juego arranca la (\d+)', txt, re.M)
        faults = txt.count('recomp: fallo')
        results[m] = (code, faults, starts[1:])
        print('mision %2d: salida %s, %d fallos%s' % (m, code, faults,
              ('; luego el juego encadena la ' + ', '.join(starts[1:])) if len(starts) > 1 else ''), flush=True)
    sheets(out, ms, sheet)
    bad = [m for m, (code, faults, _) in results.items() if code != 0 or faults]
    print('%d misiones: %d sin fallos%s' % (len(results), len(results) - len(bad),
          ('; con problemas: ' + ', '.join(map(str, bad))) if bad else ''))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
