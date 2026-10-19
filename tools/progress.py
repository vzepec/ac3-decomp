#!/usr/bin/env python3
"""progress — progreso de la etapa 6 (recompilacion del juego) para el README.

Ordenes:
  version VER  (us | eu) desde 2026-10-18: lo que mide la infografia en la version VER. Funciones
         de Ghidra de cada modulo (data/recomp*/<modulo>.json); VALIDADAS = el interprete en C
         (lo que lleva el APK) da lo mismo que el de referencia en algun caso, aleatorio
         (<modulo>_check_interp.json) o de una partida real (states_check_interp.json), y en
         ninguno distinto; EJECUTADAS en las 41 misiones (tools/cobertura.py, .json). Guarda
         solo numeros en docs/progreso.json, bajo "us" o "eu".
  count  recalcula los totales a partir del analisis local (data/ghidra, firmas PsyQ de
         ghidra_psx_ldr y cabeceras de psx/) y los guarda en docs/progreso.json. Solo
         guarda NUMEROS (cuantas funciones hay y cuantas estan hechas): nada del juego.
  svg    dibuja docs/img/progreso.svg desde docs/progreso.json (no necesita data/).

Que se mide (docs/11-etapa6.md):
  - Juego recompilado: funciones PROPIAS del juego (residente 0x800252CC-0x80099DE0, que
    es donde empieza la zona de librerias con PadEnableCom, mas cada overlay en su rango
    de carga) traducidas a C y VALIDADAS contra el interprete. Las escribe el
    recompilador en progreso.json ("recompiladas"); hasta entonces, 0.
  - Capa PsyQ: funciones de Sony a las que llama el codigo propio (con firma o
    identificadas a mano) que ya tiene psx/.
  - Pasos de la etapa y recorrido 1:1 (pantallas validadas contra DuckStation): a mano
    en progreso.json.

El texto del SVG lleva las tildes como entidades XML (&#243;...), sin tildes en el .py.

Uso:
    python3 tools/progress.py count [dir_ghidra]
    python3 tools/progress.py svg
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON = os.path.join(ROOT, 'docs', 'progreso.json')
SVG = os.path.join(ROOT, 'docs', 'img', 'progreso.svg')
FUNC_RE = re.compile(r'// ==== (\S+) @ (\S+)\n(.*?)(?=\n// ==== |\Z)', re.S)
CALL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
OWN_START, LIB_START, LIB_END, OVL_BASE = 0x800252CC, 0x80099DE0, 0x800AE098, 0x800C9DC0
OVL_END = {1: 0x800DB010, 2: 0x800F9E18, 3: 0x800DECE8, 4: 0x800D96C0, 5: 0x800CBA38}
# funciones de libreria sin firma que llama el juego, identificadas por su codigo
UNNAMED = {'FUN_8009e238': 'SpuQuit', 'FUN_8009eec8': 'SpuSetReverbVoice', 'FUN_800a11f8': 'SetVideoMode',
           'FUN_800a55b8': 'ReadGeomScreen', 'FUN_800a55e8': 'SetGeomOffset', 'FUN_800a5608': 'SetGeomScreen',
           'FUN_800a58e8': 'NormalClip', 'FUN_800aacd8': 'DecDCTReset', 'FUN_800aae30': 'DecDCTin',
           'FUN_800aaeac': 'DecDCTout', 'FUN_800aaf08': 'DecDCToutSync', 'FUN_800aaf74': 'DecDCToutCallback',
           'FUN_800ac328': 'strlen', 'FUN_800a53a8': 'TransMatrix', 'FUN_800ad7f8': 'ExitCriticalSection', 'FUN_800ad7e8': 'EnterCriticalSection'}
# nombres de la capa para funciones que el ACE.BIN firma mal, o que van al anfitrion
ALIAS = {'SsUtReverbOff': 'SpuInit', 'SpuRead': 'SpuWrite', 'rand': 'psx_rand', 'srand': 'psx_srand',
         'printf': 'psx_printf', 'puts': 'psx_puts', '__divdi3': 'psx_divdi3', '__udivdi3': 'psx_udivdi3',
         'sprintf': 'psx_sformat', 'open': 'bios_open', 'read': 'bios_read', 'write': 'bios_write',
         'close': 'bios_close', 'firstfile': 'bios_firstfile', 'nextfile': 'bios_nextfile', 'erase': 'bios_erase'}
HOST = {'memset', 'memcpy', 'memmove', 'memchr', 'strlen', 'strcmp', 'strncmp', 'strcpy', 'strcat', 'bzero', '__main'}


def funcs(path):
    return [(n, int(a, 16), b) for n, a, b in FUNC_RE.findall(open(path, encoding='latin1').read())]


def load_sigs(ghidra):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from psyqapi import load_sigs as ls
    return ls(os.path.join(ghidra, 'Ghidra', 'Extensions', 'ghidra_psx_ldr', 'data', 'psyq', '450'))


def cmd_count(ghidra):
    sym = load_sigs(ghidra)
    data = json.load(open(JSON)) if os.path.exists(JSON) else {}
    mods = {}
    res = funcs(os.path.join(ROOT, 'data', 'ghidra', 'all_ace_eu.c'))
    own = {n: b for n, a, b in res if (OWN_START <= a < LIB_START or 0x800AE118 <= a < 0x800AE528) and n not in sym}
    libaddr = {n: a for n, a, b in res if LIB_START <= a <= LIB_END or n in sym}
    mods['residente'] = len(own)
    bodies = dict(own)
    for k, end in OVL_END.items():
        f = funcs(os.path.join(ROOT, 'data', 'ghidra', 'all_overlay%d.c' % k))
        o = {n: b for n, a, b in f if OVL_BASE <= a < end}
        mods['overlay %d' % k] = len(o)
        bodies.update({'ovl%d:%s' % (k, n): b for n, b in o.items()})
    # los overlays llaman al residente por direccion (FUN_800xxxxx en su volcado): se
    # traduce al nombre que tiene en el residente
    byaddr = {a: n for n, a, b in res}
    called = set()
    for b in bodies.values():
        for c in set(CALL_RE.findall(b)):
            if c.startswith('FUN_') and c not in libaddr:
                try:
                    c = byaddr.get(int(c[4:], 16), c)
                except ValueError:
                    pass
            if c in libaddr or c in UNNAMED:
                called.add(UNNAMED.get(c, c))
    impl = set()
    for h in glob.glob(os.path.join(ROOT, 'psx', 'include', '*.h')):
        impl |= set(CALL_RE.findall(open(h).read()))
    done = sorted(c for c in called if c in impl or ALIAS.get(c) in impl or c in HOST)
    missing = sorted(called - set(done))
    data['funciones'] = mods
    data.setdefault('recompiladas', {m: 0 for m in mods})
    data['capa'] = {'necesarias': len(called), 'hechas': len(done), 'faltan': missing}
    data.setdefault('pasos', {'total': 8, 'hechos': 0, 'actual': 'Paso 1: completar la capa'})
    data.setdefault('recorrido', {'total': 46, 'validadas': 0,
                                  'nota': 'arranque, titulo, menu, briefing, 41 misiones (registros 6-46), final'})
    json.dump(data, open(JSON, 'w'), indent=1, ensure_ascii=False)
    print('funciones propias:', mods, '= %d' % sum(mods.values()))
    print('capa: %d/%d; faltan: %s' % (len(done), len(called), ', '.join(missing)))


def cmd_version(ver, cov_json):
    rdir = os.path.join(ROOT, 'data', 'recomp_us' if ver == 'us' else 'recomp')
    stem = 'ace_us' if ver == 'us' else 'ace_eu'
    data = json.load(open(JSON)) if os.path.exists(JSON) else {}
    cov = json.load(open(cov_json)) if cov_json and os.path.exists(cov_json) else {}
    st_path = os.path.join(rdir, 'states_check_interp.json')
    states = json.load(open(st_path)) if os.path.exists(st_path) else {}
    out = {'funciones': {}, 'validadas': {}, 'ejecutadas': {}, 'distintas': 0}
    for mod, name in [('residente', stem)] + [('overlay %d' % k, 'overlay%d' % k) for k in range(1, 6)]:
        meta = json.load(open(os.path.join(rdir, name + '.json')))
        gh = {m['addr'] for m in meta if m.get('ghidra')}
        verd = {}
        p = os.path.join(rdir, name + '_check_interp.json')
        for src in ([json.load(open(p))] if os.path.exists(p) else []) + [states.get(mod, {})]:
            for k, v in src.items():
                verd.setdefault(int(k, 16), set()).add(v['veredicto'])
        val = [a for a in gh if 'VALIDADA' in verd.get(a, ()) and 'DISTINTA' not in verd.get(a, ())]
        out['distintas'] += sum(1 for a in gh if 'DISTINTA' in verd.get(a, ()))
        out['funciones'][mod] = len(gh)
        out['validadas'][mod] = len(val)
        if mod in cov:
            out['ejecutadas'][mod] = cov[mod]['ejecutadas']
    data[ver] = out
    json.dump(data, open(JSON, 'w'), indent=1, ensure_ascii=False)
    print('%s: %d/%d validadas (%d distintas), %d ejecutadas' % (ver, sum(out['validadas'].values()),
          sum(out['funciones'].values()), out['distintas'], sum(out['ejecutadas'].values())))


def bar(x, y, w, frac, color, h=18):
    return ('<rect x="%d" y="%d" width="%d" height="%d" rx="4" fill="#30363d"/>'
            '<rect x="%d" y="%d" width="%.1f" height="%d" rx="4" fill="%s"/>'
            % (x, y, w, h, x, y, max(0.0, min(1.0, frac)) * w, h, color))


def cmd_svg():
    d = json.load(open(JSON))
    us = d.get('us')
    if not us:   # sin los datos de la americana: no hay nada nuevo que dibujar
        sys.exit('falta "us" en docs/progreso.json: tools/progress.py version us <cobertura.json>')
    mods = us['funciones']
    total = sum(mods.values())
    val = sum(us['validadas'].values())
    ex = sum(us['ejecutadas'].values())
    W, H = 840, 560
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
           'font-family="Segoe UI, Helvetica, Arial, sans-serif">' % (W, H, W, H),
           '<rect width="%d" height="%d" rx="12" fill="#0d1117"/>' % (W, H),
           '<text x="24" y="38" fill="#e6edf3" font-size="20" font-weight="600">Ace Combat 3 nativo: progreso</text>',
           '<text x="24" y="60" fill="#8b949e" font-size="13">Versi&#243;n americana (SLUS-00972), la de referencia '
           '&#183; Etapa 6: %s &#183; Etapa 7: APK libre publicado (%s)</text>' % (d['pasos']['actual'], d.get('release', ''))]
    pct = 100.0 * val / total if total else 0
    out.append('<text x="24" y="100" fill="#e6edf3" font-size="15">C&#243;digo del juego validado (int&#233;rprete del APK '
               'frente al de referencia)</text>')
    out.append('<text x="%d" y="100" fill="#e6edf3" font-size="15" text-anchor="end" font-weight="600">'
               '%.1f %% &#183; %d / %d funciones</text>' % (W - 24, pct, val, total))
    out.append(bar(24, 110, W - 48, val / total if total else 0, '#2ea043', 26))
    y = 160
    colw = (W - 48 - 3 * 16) // 3
    for i, (m, n) in enumerate(mods.items()):
        cx, cy = 24 + (i % 3) * (colw + 16), y + (i // 3) * 42
        r = us['validadas'].get(m, 0)
        out.append('<text x="%d" y="%d" fill="#8b949e" font-size="12">%s</text>' % (cx, cy, m))
        out.append('<text x="%d" y="%d" fill="#8b949e" font-size="12" text-anchor="end">%d / %d</text>'
                   % (cx + colw, cy, r, n))
        out.append(bar(cx, cy + 6, colw, r / n if n else 0, '#2ea043', 10))
    y = 262
    rec = d['recorrido']
    rows = [('Funciones que se ejecutan en las 41 misiones', ex, total, '#3fb950'),
            ('Capa PsyQ (funciones de Sony que usa el juego)', d['capa']['hechas'], d['capa']['necesarias'], '#1f6feb'),
            ('Pasos de la etapa 6', d['pasos']['hechos'], d['pasos']['total'], '#a371f7'),
            ('Recorrido: funciona (arranque, men&#250;s, 41 misiones; falta el final)', rec['funcionan'], rec['total'], '#d29922'),
            ('Recorrido: comparado 1:1 con DuckStation', rec['duckstation'], rec['total'], '#db6d28')]
    for label, a, b, col in rows:
        out.append('<text x="24" y="%d" fill="#e6edf3" font-size="14">%s</text>' % (y, label))
        out.append('<text x="%d" y="%d" fill="#e6edf3" font-size="14" text-anchor="end">%d / %d &#183; %.0f %%</text>'
                   % (W - 24, y, a, b, 100.0 * a / b if b else 0))
        out.append(bar(24, y + 8, W - 48, a / b if b else 0, col, 14))
        y += 46
    eu = d.get('eu')
    if eu:
        out.append('<text x="24" y="%d" fill="#8b949e" font-size="13">Versi&#243;n europea (SCES-02066): %d / %d funciones '
                   'validadas &#183; %d ejecutadas en las misiones &#183; misiones: %s</text>'
                   % (y + 4, sum(eu['validadas'].values()), sum(eu['funciones'].values()), sum(eu['ejecutadas'].values()),
                      eu.get('misiones', '?')))
    out.append('</svg>')
    os.makedirs(os.path.dirname(SVG), exist_ok=True)
    open(SVG, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print('%s: americana %.1f %% validada (%d/%d), %d ejecutadas' % (os.path.relpath(SVG, ROOT), pct, val, total, ex))


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ('count', 'svg', 'version'):
        sys.exit(__doc__)
    if sys.argv[1] == 'version':
        cmd_version(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    if sys.argv[1] == 'count':
        cmd_count(sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser('~/tools/ghidra_12.1.4_PUBLIC'))
    cmd_svg()


if __name__ == '__main__':
    main()
