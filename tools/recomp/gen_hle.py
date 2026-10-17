#!/usr/bin/env python3
"""gen_hle — genera el enlace NATIVO entre el codigo recompilado y la capa psx/ (etapa 6).

Por cada funcion de libreria de Sony a la que salta el juego (tools/recomp/hle.py
lib_targets: 155 destinos) escribe una funcion de C que toma los argumentos de los
registros MIPS (a0-a3 y la pila desde sp+16), convierte los punteros PS1 a punteros de C
(psx_ptr; 0 sigue siendo NULL), llama a la implementacion de psx/ y deja el resultado en
v0 (los punteros, de vuelta a direccion PS1). Firmas: los prototipos de psx/include/*.h.
Casos aparte, como en hle.py: alias (SsUtReverbOff = SpuInit...), printf/sprintf,
memset/memchr/strlen/strcpy/strcat, __divdi3/__udivdi3, __main. Ademas escribe la firma
(primeros 64 bytes) de cada overlay, para saber cual hay cargado.
El resultado contiene direcciones y bytes del ejecutable del juego: va a data/recomp/.

Etapa 7 (--libre SUFIJO): la misma tabla SIN las firmas de los overlays (los unicos bytes del
juego): solo direcciones, nombres de funciones de libreria y envoltorios generados de nuestras
cabeceras. Es dato de analisis, sin codigo de Namco, y se versiona en psx/recomp/hle_<sufijo>.c
(tablas hle_table_<sufijo> y hle_count_<sufijo>) para el APK distribuible:
    python3 tools/recomp/gen_hle.py psx/recomp/hle_eu.c --libre eu
    python3 tools/recomp/gen_hle.py psx/recomp/hle_us.c --libre us --region data/recomp_us/region.json ...

Uso:
    python3 tools/recomp/gen_hle.py data/recomp/hle_gen.c
    python3 tools/recomp/gen_hle.py data/recomp_us/hle_gen.c --region data/recomp_us/region.json \
        data/exe/ace_us.bin data/exe/ovl_us data/ghidra_us/all_ace_us.c      (otra version)
"""
import json
import glob
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from hle import ALIAS, lib_targets  # noqa: E402

SPECIAL = {
    '__main': 'c->r[2] = 0;',
    'VSync': 'c->r[2] = (uint32_t)VSync((int)hle_arg(c, 0)); rc_vsync_hook();',
    'memset': '{ uint32_t i; for (i = 0; i < c->r[6]; i++) rc_sb(c->r[4] + i, c->r[5], 0); c->r[2] = c->r[4]; }',
    'memchr': '{ uint32_t i; c->r[2] = 0; for (i = 0; i < c->r[6]; i++) if (rc_lb(c->r[4] + i, 0) == (c->r[5] & 0xFF)) { c->r[2] = c->r[4] + i; break; } }',
    'strlen': '{ uint32_t n = 0; while (rc_lb(c->r[4] + n, 0)) n++; c->r[2] = n; }',
    'strcpy': '{ uint32_t i = 0; uint8_t b; do { b = (uint8_t)rc_lb(c->r[5] + i, 0); rc_sb(c->r[4] + i, b, 0); i++; } while (b); c->r[2] = c->r[4]; }',
    'strcat': '{ uint32_t d = c->r[4], i = 0; uint8_t b; while (rc_lb(d, 0)) d++; do { b = (uint8_t)rc_lb(c->r[5] + i, 0); rc_sb(d + i, b, 0); i++; } while (b); c->r[2] = c->r[4]; }',
    '__divdi3': '{ int64_t a = (int64_t)((uint64_t)c->r[5] << 32 | c->r[4]), b = (int64_t)((uint64_t)c->r[7] << 32 | c->r[6]); uint64_t q = (uint64_t)psx_divdi3(a, b); c->r[2] = (uint32_t)q; c->r[3] = (uint32_t)(q >> 32); }',
    '__udivdi3': '{ uint64_t a = (uint64_t)c->r[5] << 32 | c->r[4], b = (uint64_t)c->r[7] << 32 | c->r[6]; uint64_t q = psx_udivdi3(a, b); c->r[2] = (uint32_t)q; c->r[3] = (uint32_t)(q >> 32); }',
    'printf': '{ uint32_t a[24]; int k; for (k = 0; k < 24; k++) a[k] = hle_arg(c, 1 + k); c->r[2] = (uint32_t)psx_printf((const char *)rc_hptr(c->r[4]), a); }',
    'sprintf': '{ uint32_t a[24]; int k; char buf[2048]; for (k = 0; k < 24; k++) a[k] = hle_arg(c, 2 + k); int n = psx_sformat(buf, sizeof buf, (const char *)rc_hptr(c->r[5]), a); for (k = 0; k <= n && k < (int)sizeof buf; k++) rc_sb(c->r[4] + k, k < n ? (uint8_t)buf[k] : 0, 0); c->r[2] = (uint32_t)n; }',
}


def parse_protos():
    """{nombre: (retorno, [tipo de cada argumento])} con los tipos de C tal cual"""
    out = {}
    for h in glob.glob(os.path.join(ROOT, 'psx', 'include', '*.h')):
        src = open(h).read()
        src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
        src = re.sub(r'#.*', ' ', src)
        src = re.sub(r'typedef[^;]*;', ' ', src)
        src = re.sub(r'\{[^{}]*\}', ' ', src)
        for m in re.finditer(r'([A-Za-z_][\w\s\*]*?)\b([A-Za-z_]\w*)\s*\(([^()]*)\)\s*;', src):
            ret, name, args = ' '.join(m.group(1).split()), m.group(2), m.group(3).strip()
            types = []
            if args not in ('', 'void'):
                for a in args.split(','):
                    a = ' '.join(a.split())
                    mm = re.match(r'(.*?)(\w+)$', a)
                    t = mm.group(1).strip() if mm and mm.group(1).strip() else a
                    types.append(t)
            out[name] = (ret, types)
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    acebin = os.path.join(ROOT, 'data', 'exe', 'ace_eu.bin')
    ovl_dir = os.path.join(ROOT, 'data', 'exe', 'ovl')
    region, ghidra_c = None, None
    libre = None
    if '--libre' in sys.argv:
        libre = sys.argv[sys.argv.index('--libre') + 1]
    if '--region' in sys.argv:
        i = sys.argv.index('--region')
        region = json.load(open(sys.argv[i + 1]))
        acebin, ovl_dir, ghidra_c = sys.argv[i + 2], sys.argv[i + 3], sys.argv[i + 4]
    ovls = sorted(glob.glob(os.path.join(ovl_dir, 'overlay*.exe')))
    targets = lib_targets(acebin, ovls, region, ghidra_c)
    protos = parse_protos()
    head = (['/* GENERADO por tools/recomp/gen_hle.py --libre: enlace del codigo del juego con psx/ (version %s).' % libre.upper(),
             ' * Solo direcciones y nombres de funciones de libreria (dato de analisis) y envoltorios generados de',
             ' * psx/include: sin bytes del juego, se versiona (etapa 7, docs/12-etapa7.md). */']
            if libre else
            ['/* GENERADO por tools/recomp/gen_hle.py: enlace nativo codigo recompilado -> psx/.',
             ' * Contiene direcciones y bytes del ejecutable del juego: no entra en git. */'])
    lines = head + [
             '#include <stdint.h>', '#include "recomp_rt.h"']
    for h in sorted(glob.glob(os.path.join(ROOT, 'psx', 'include', 'psx_lib*.h'))) + \
            [os.path.join(ROOT, 'psx', 'include', 'psx_libc.h')]:
        lines.append('#include "%s"' % os.path.basename(h))
    lines += ['', 'static void *rc_hptr(uint32_t a) { return a ? psx_ptr(a) : 0; }',
              'static uint32_t rc_pptr(const void *p) { return p ? psx_addr(p) : 0; }',
              'static uint32_t hle_arg(RCpu *c, int k) { return k < 4 ? c->r[4 + k] : rc_lw(c->r[29] + 16 + 4 * (k - 4), 0); }', '']
    table = []
    for addr in sorted(targets):
        name = targets[addr]
        fn = 'h_%08x' % addr
        if name in SPECIAL:
            body = SPECIAL[name]
        else:
            cname = ALIAS.get(name, name)
            if cname not in protos:
                raise SystemExit('sin prototipo: %s (%s)' % (name, cname))
            ret, types = protos[cname]
            args = []
            for k, t in enumerate(types):
                v = 'hle_arg(c, %d)' % k
                args.append('(%s)rc_hptr(%s)' % (t, v) if '*' in t else '(%s)%s' % (t, v))
            call = '%s(%s)' % (cname, ', '.join(args))
            if ret == 'void':
                body = '%s; c->r[2] = 0;' % call
            elif '*' in ret:
                body = 'c->r[2] = rc_pptr(%s);' % call
            else:
                body = 'c->r[2] = (uint32_t)(%s);' % call
        lines.append('static void %s(RCpu *c) { %s }   /* %s */' % (fn, body, name))
        table.append((addr, fn))
    lines.append('')
    sfx = '_' + libre if libre else ''
    lines.append('const RcFunc hle_table%s[] = {' % sfx)
    lines += ['    {0x%08xu, %s},' % (a, f) for a, f in table]
    lines.append('};')
    lines.append('const unsigned hle_count%s = %d;' % (sfx, len(table)))
    lines.append('')
    if libre:
        open(sys.argv[1], 'w').write('\n'.join(lines) + '\n')
        print('%d funciones de libreria enlazadas (sin bytes del juego) -> %s' % (len(table), sys.argv[1]))
        return 0
    lines.append('/* firmas de los overlays (primeros 64 bytes cargados en 0x800C9DC0) */')
    lines.append('const uint8_t ovl_sig[5][64] = {')
    for p in ovls:
        d = open(p, 'rb').read()[2048:2048 + 64]
        lines.append('    {' + ','.join(str(b) for b in d) + '},')
    lines.append('};')
    open(sys.argv[1], 'w').write('\n'.join(lines) + '\n')
    print('%d funciones de libreria enlazadas -> %s' % (len(table), sys.argv[1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
