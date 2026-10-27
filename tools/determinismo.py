#!/usr/bin/env python3
"""determinismo — da la misma partida el mismo mando? (etapa 9, paso 1: base del cooperativo en red).

Un cooperativo por lockstep (los dos aparatos simulan la misma partida con los mandos de los dos)
solo funciona si la simulacion es determinista: mismos mandos en los mismos VBlank -> misma RAM.
Ademas los dos aparatos pueden tener opciones distintas (60/120 Hz, 16:9, GPU...): lo que solo
cambia la imagen no debe cambiar la RAM de la partida.

Lanza el binario libre (make libre-build: psx/build/ac3_libre, el mismo codigo que el APK) con el
mismo guion de mando y una configuracion distinta por ejecucion. Cada ejecucion anota con
AC3_RAMHASH la huella de la RAM en cada VBlank (32 bloques de 64 KB, el scratchpad y la semilla de
rand, que esta fuera de la RAM). Despues
compara cada configuracion con la primera ("base"): el primer VBlank distinto, los bloques que
difieren y en cuantos VBlank. Con --detalle vuelca la RAM entera de las dos en ese VBlank
(AC3_DUMPMEM) y lista los tramos de bytes distintos.

--apk: la SPU avanza en todas (AC3_SPU=1), como en el APK; sin ella el PC no la avanza y la
configuracion "spu" sale distinta (el juego lee el estado de las voces: SpuGetAllKeysStatus).

Uso:
    python3 tools/determinismo.py --comparar <base.hash> <otra.hash> [...]
    python3 tools/determinismo.py <imagen_del_disco> <salida> [--mision N] [--fotogramas F]
                                  [--eu] [--apk] [--detalle] [--solo cfg1,cfg2,...]
    p. ej. python3 tools/determinismo.py "Rom/.../...(USA).bin" psx/build/determinismo --mision 0
La configuracion "control_mando" (un misil mas en el VBlank 3000) es el caso positivo: debe salir
distinta; si no, la medida no ve las diferencias. Las "opcion_*" (16:9) cambian datos que el juego
calcula de la imagen: se anotan, pero no cuentan como fallo (en red, el anfitrion las fija para todos).
Devuelve 0 si todas las configuraciones dan la misma RAM que la base en todos los VBlank (y el
control, distinta).
"""
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import misiones  # noqa: E402  (guiones de mando hasta la mision, US y EU)

# configuracion -> variables de entorno (sobre AC3_PAD_ID=73 y AC3_MISSION)
CONFIGS = [
    ('base', {}),
    ('base_otra_vez', {}),                                       # la misma: debe dar lo mismo
    ('control_mando', {'_guion': ',3000:circulo:2'}),            # control: un misil mas; DEBE salir distinta
    ('spu', {'AC3_SPU': '1'}),                                   # la SPU avanza como en el APK
    ('intermedias_60', {'AC3_RR': '1', 'AC3_INTERP': '1', 'AC3_INTERP_THREADS': '1'}),
    ('intermedias_120', {'AC3_RR': '1', 'AC3_INTERP': '3', 'AC3_INTERP_THREADS': '3'}),
    ('opcion_ancha', {'AC3_WIDE': '1'}),                         # 16:9: cambia la RAM (opcion de la partida)
    ('gpu', {'AC3_HW_NULL': '1', 'AC3_HW_MAIN': '1'}),           # imagen solo en la GPU en la mision
    ('ritmo_real', {'AC3_PACE': '60', 'AC3_RR': '1', 'AC3_INTERP': '3', 'AC3_INTERP_THREADS': '3'}),
]
NB = 35  # enteros por VBlank: VBlank, 32 bloques de RAM, scratchpad, semilla de rand


def read_hashes(path):
    data = open(path, 'rb').read()
    n = len(data) // (NB * 8)
    out = {}
    for i in range(n):
        rec = struct.unpack_from('<%dQ' % NB, data, i * NB * 8)
        out[rec[0]] = rec[1:]
    return out


def block_name(b):
    if b == 33:
        return 'semilla de rand'
    return 'scratchpad' if b == 32 else '0x%08X-0x%08X' % (0x80000000 + b * 0x10000, 0x80000000 + (b + 1) * 0x10000 - 1)


APK = {}


def run(exe, disc, out, name, env_extra, mission, frames, guion, dump=None):
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    guion += env_extra.get('_guion', '')
    env = dict(os.environ, AC3_PAD_ID='73', AC3_RAMHASH=os.path.join(d, 'ram.hash'), **APK)
    env.update({k: v for k, v in env_extra.items() if not k.startswith('_')})
    if mission is not None:
        env['AC3_MISSION'] = str(mission)
    if dump:
        env['AC3_DUMPMEM'] = '%d,80000000,200000,%s' % (dump, os.path.join(d, 'ram_%d.bin' % dump))
    with open(os.path.join(d, 'log.txt'), 'w') as log:
        code = subprocess.call([exe, disc, '-', str(frames), d, '0', guion], env=env, stdout=log,
                               stderr=subprocess.STDOUT, cwd=ROOT, timeout=1800)
    return code, os.path.join(d, 'ram.hash')


def compare(base, h, name):
    """compara dos huellas; devuelve el primer VBlank distinto o None"""
    common = sorted(set(base) & set(h))
    diff = [v for v in common if base[v] != h[v]]
    if not diff:
        print('%-16s = base en los %d VBlank (%d-%d)' % (name, len(common), common[0] if common else 0,
                                                       common[-1] if common else 0))
        return None
    first = diff[0]
    blocks = [b for b in range(NB - 1) if base[first][b] != h[first][b]]
    per_block = {}
    for v in diff:
        for b in range(NB - 1):
            if base[v][b] != h[v][b]:
                per_block[b] = per_block.get(b, 0) + 1
    print('%-16s DISTINTA: primer VBlank %d (bloques %s); distinta en %d de %d VBlank; al final %s'
          % (name, first, ', '.join(block_name(b) for b in blocks), len(diff), len(common),
             'distinta' if common[-1] in diff else 'igual'))
    for b, k in sorted(per_block.items()):
        print('    %-24s distinto en %d VBlank' % (block_name(b), k))
    return first


def ranges(a, b):
    out, i, n = [], 0, min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and (a[j] != b[j] or (j + 16 < n and a[j:j + 16] != b[j:j + 16])):
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def main():
    opts = sys.argv[1:]
    if opts[:1] == ['--comparar']:   # huellas ya anotadas (p. ej. las del APK: huella=1)
        if len(opts) < 3:
            sys.exit(__doc__)
        base, bad = read_hashes(opts[1]), 0
        print('base: %s, %d VBlank' % (opts[1], len(base)))
        for path in opts[2:]:
            bad += compare(base, read_hashes(path), os.path.basename(path)) is not None
        return 1 if bad else 0
    args, i = [], 0
    while i < len(opts):   # las opciones con valor se saltan con su valor
        if opts[i] in ('--mision', '--fotogramas', '--solo'):
            i += 2
            continue
        if not opts[i].startswith('--'):
            args.append(opts[i])
        i += 1
    if len(args) < 2:
        sys.exit(__doc__)
    disc, out = args[0], args[1]

    def opt(name, default=None):
        return opts[opts.index(name) + 1] if name in opts else default
    mission = int(opt('--mision')) if opt('--mision') is not None else 0
    eu = '--eu' in opts
    if '--apk' in opts:
        APK['AC3_SPU'] = '1'
    frames = int(opt('--fotogramas', misiones.FRAMES_EU if eu else misiones.FRAMES))
    guion = misiones.GUION_EU if eu else misiones.GUION
    solo = opt('--solo')
    cfgs = [c for c in CONFIGS if c[0] in ('base', 'control_mando') or not solo or c[0] in solo.split(',')]
    exe = os.path.join(ROOT, 'psx', 'build', 'ac3_libre')
    if not os.path.exists(exe):
        sys.exit('falta psx/build/ac3_libre: make libre-build')
    print('mision %d, %d VBlank, version %s%s' % (mission, frames, 'europea' if eu else 'americana',
                                                ', SPU como en el APK' if APK else ''), flush=True)
    hashes, bad = {}, 0
    for name, env in cfgs:
        code, path = run(exe, disc, out, name, env, mission, frames, guion)
        hashes[name] = read_hashes(path)
        print('  %-16s salida %s, %d VBlank anotados' % (name, code, len(hashes[name])), flush=True)
    base = hashes['base']
    for name, _ in cfgs[1:]:
        control = name.startswith('control')
        first = compare(base, hashes[name], name)
        if first is None:
            if control:
                print('    <- FALLA EL CONTROL: la medida no ve la diferencia')
            bad += control
            continue
        if name.startswith('opcion'):
            print('    (opcion de la partida: los dos aparatos deben tenerla igual; no cuenta como fallo)')
            continue
        bad += not control
        if '--detalle' in opts:
            env = dict(cfgs)[name]
            run(exe, disc, out, 'base', {}, mission, first + 1, guion, dump=first)
            run(exe, disc, out, name, env, mission, first + 1, guion, dump=first)
            a = open(os.path.join(out, 'base', 'ram_%d.bin' % first), 'rb').read()
            c = open(os.path.join(out, name, 'ram_%d.bin' % first), 'rb').read()
            rs = ranges(a, c)
            print('    VBlank %d: %d tramos distintos' % (first, len(rs)))
            for i, j in rs[:40]:
                print('      0x%08X-0x%08X (%d bytes)' % (0x80000000 + i, 0x80000000 + j - 1, j - i))
    print('%d configuraciones (con el control): %d como se espera, %d no' % (len(cfgs) - 1, len(cfgs) - 1 - bad, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
