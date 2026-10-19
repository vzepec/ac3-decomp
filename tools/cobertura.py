#!/usr/bin/env python3
"""cobertura — que funciones del juego se ejecutan en partidas reales (etapa 6, paso 12).

Lee los cobertura.txt que deja el binario libre con AC3_COVERAGE (tools/misiones.py lo pone en
cada mision): direcciones en las que el interprete entro como funcion; en la zona de overlays,
"direccion firma", con la firma = FNV-1a de los 64 primeros bytes cargados en la base, que se
compara con la de cada overlay (data/exe/ovl*/overlayN.exe) para saber cual estaba cargado.
Lo cruza con la lista de funciones de Ghidra de cada modulo (data/recomp*/<modulo>.json) y con el
veredicto del comparador con estados aleatorios (<modulo>_check.json) y da, por modulo:
funciones de Ghidra, ejecutadas en partida, validadas, y ejecutadas pero SIN CASO (candidatas a
validarse con estados reales: tools/recomp/check.py --states).

Uso:
    python3 tools/cobertura.py us <dir_con_cobertura.txt> [salida.json]
"""
import glob
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fnv64(d):
    h = 2166136261
    for b in d[:64]:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ver, src = sys.argv[1], sys.argv[2]
    rdir = os.path.join(ROOT, 'data', 'recomp_us' if ver == 'us' else 'recomp')
    odir = os.path.join(ROOT, 'data', 'exe', 'ovl_us' if ver == 'us' else 'ovl')
    stem = 'ace_us' if ver == 'us' else 'ace_eu'
    sig = {}
    for k in range(1, 6):
        d = open(os.path.join(odir, 'overlay%d.exe' % k), 'rb').read()
        sig[fnv64(d[2048:])] = k
    resident, ovl = set(), {k: set() for k in range(1, 6)}
    unknown = 0
    files = glob.glob(os.path.join(src, '**', 'cobertura.txt'), recursive=True)
    for p in files:
        for line in open(p):
            f = line.split()
            if not f:
                continue
            a = int(f[0], 16)
            if len(f) == 1:
                resident.add(a)
            elif int(f[1], 16) in sig:
                ovl[sig[int(f[1], 16)]].add(a)
            else:
                unknown += 1
    out, tot = {}, [0, 0, 0, 0]
    print('%d archivos de cobertura (%d entradas en overlays sin identificar)' % (len(files), unknown))
    print('%-10s %8s %10s %10s %16s' % ('modulo', 'Ghidra', 'ejecutadas', 'validadas', 'ejec. sin caso'))
    for mod, name, entered in [('residente', stem, resident)] + [('overlay %d' % k, 'overlay%d' % k, ovl[k]) for k in range(1, 6)]:
        meta = json.load(open(os.path.join(rdir, name + '.json')))
        chk = json.load(open(os.path.join(rdir, name + '_check.json')))
        gh = [m['addr'] for m in meta if m.get('ghidra')]
        ver_of = {int(k, 16): v['veredicto'] for k, v in chk.items()}
        ex = [a for a in gh if a in entered]
        val = [a for a in gh if ver_of.get(a) == 'VALIDADA']
        pend = sorted(a for a in ex if ver_of.get(a) != 'VALIDADA')
        print('%-10s %8d %10d %10d %16d' % (mod, len(gh), len(ex), len(val), len(pend)))
        out[mod] = {'ghidra': len(gh), 'ejecutadas': len(ex), 'validadas': len(val),
                    'ejecutadas_sin_caso': ['%08x' % a for a in pend],
                    'sin_caso_no_ejecutadas': ['%08x' % a for a in gh if ver_of.get(a) != 'VALIDADA' and a not in entered]}
        for i, v in enumerate((len(gh), len(ex), len(val), len(pend))):
            tot[i] += v
    print('%-10s %8d %10d %10d %16d' % ('total', *tot))
    if len(sys.argv) > 3:
        json.dump(out, open(sys.argv[3], 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
