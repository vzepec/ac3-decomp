#!/usr/bin/env python3
"""guardado — prueba de guardar y cargar partida en la tarjeta de memoria (etapa 6, paso 13).

Con el binario libre (make libre-build) y una tarjeta nueva (AC3_CARD), dos partidas:
  1. GUARDAR: partida nueva -> interfaz de la UPEO (antes de la primera mision) -> SAVE ->
     "Save game? yes" -> "No files found... Save new file? yes" -> archivo 1 -> "Save to file #1?"
     (cursor arriba = yes) -> guardado. Se comprueba en el registro de la capa (PSX_CARDDBG) que
     el juego crea el archivo (open con FCREAT), lo escribe y lo vuelve a leer, y en la tarjeta
     que hay una partida del juego: entrada de directorio BASLUS-00972AC3NA (US) / BESCES-...
     (EU), bloque con cabecera "SC" y titulo en Shift-JIS.
  2. CARGAR: otra partida con una COPIA de esa tarjeta -> menu principal -> load -> archivo 1 ->
     confirmar -> el juego lee la partida y vuelve a la interfaz de la UPEO (captura).
Los guiones son de cada version (la europea tiene menu de idioma y va ~900 fotogramas por detras).

Uso:
    python3 tools/guardado.py <imagen_del_disco> <salida> [--eu]
Devuelve 0 si se guarda y se carga.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUIONES = {
    'us': ('300:start:8,500:cruz:6,700:cruz:6,900:cruz:6,1080:derecha:6,1110:derecha:6,1200:cruz:6,1300:cruz:6,'
           '1450:cruz:6,1600:cruz:6,1750:cruz:6,1880:arriba:6,1950:cruz:6,2150:cruz:6,2350:cruz:6', 2600,
           '300:start:8,480:abajo:6,520:cruz:6,700:cruz:6,850:arriba:6,900:cruz:6,1100:cruz:6,1300:cruz:6', 1800),
    'eu': ('320:cruz:6,1100:start:8,1400:cruz:6,1600:cruz:6,1800:cruz:6,1980:derecha:6,2010:derecha:6,2100:cruz:6,'
           '2200:cruz:6,2350:cruz:6,2500:cruz:6,2650:cruz:6,2780:arriba:6,2850:cruz:6,3050:cruz:6,3250:cruz:6', 3500,
           '320:cruz:6,1100:start:8,1300:abajo:6,1340:cruz:6,1520:cruz:6,1670:arriba:6,1720:cruz:6,1920:cruz:6,'
           '2120:cruz:6', 2700),
}


def run(exe, disc, card, out, frames, guion):
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, AC3_CARD=card, PSX_CARDDBG='1')
    p = subprocess.run([exe, disc, '-', str(frames), out, '100', guion], env=env, capture_output=True, text=True,
                       errors='replace', cwd=ROOT, timeout=900)
    log = p.stdout + p.stderr
    open(os.path.join(out, 'log.txt'), 'w').write(log)
    return p.returncode, [l for l in log.splitlines() if l.startswith('tarjeta:') and 'card_op' not in l], log.count('recomp: fallo')


def card_saves(path):
    d = open(path, 'rb').read()
    out = []
    for k in range(1, 16):
        e = d[128 * k:128 * k + 128]
        if int.from_bytes(e[0:4], 'little') == 0x51:
            b = d[8192 * k:8192 * k + 8192]
            title = b[4:68].split(b'\0')[0].decode('shift_jis', 'replace')
            out.append((e[10:30].split(b'\0')[0].decode('ascii', 'replace'), b[:2] == b'SC', title))
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ver = 'eu' if '--eu' in sys.argv else 'us'
    argv = [a for a in sys.argv if a != '--eu']
    disc, out = argv[1], argv[2]
    exe = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
    gs, fs, gl, fl = GUIONES[ver]
    os.makedirs(out, exist_ok=True)
    card = os.path.join(out, 'tarjeta.mcd')
    if os.path.exists(card):
        sys.exit('ya existe %s: usa otra salida' % card)
    code, calls, faults = run(exe, disc, card, os.path.join(out, 'guardar'), fs, gs)
    created = any(re.search(r'open\(.*, 0x[0-9a-f]*200\) = [0-9]', c) for c in calls)
    written = [c for c in calls if c.startswith('tarjeta: write(') and not c.endswith('= -1')]
    saves = card_saves(card)
    ok_save = code == 0 and not faults and created and written and saves and all(s[1] for s in saves)
    print('guardar: salida %d, %d fallos; archivo creado: %s; %s; partidas en la tarjeta: %s' % (
        code, faults, 'si' if created else 'NO', written[0][9:] if written else 'sin escritura',
        ', '.join('%s (SC %s, "%s")' % s for s in saves) or 'ninguna'))
    card2 = os.path.join(out, 'tarjeta_copia.mcd')
    open(card2, 'wb').write(open(card, 'rb').read())
    code, calls, faults = run(exe, disc, card2, os.path.join(out, 'cargar'), fl, gl)
    reads = [c for c in calls if c.startswith('tarjeta: read(') and not c.endswith('= -1') and not c.endswith('= 0')]
    ok_load = code == 0 and not faults and reads
    print('cargar: salida %d, %d fallos; lecturas de la partida: %d (%s)' % (code, faults, len(reads), reads[0][9:] if reads else '-'))
    print('RESULTADO: %s' % ('guarda y carga' if ok_save and ok_load else 'FALLA'))
    return 0 if ok_save and ok_load else 1


if __name__ == '__main__':
    sys.exit(main())
