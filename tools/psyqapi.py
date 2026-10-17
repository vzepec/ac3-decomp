#!/usr/bin/env python3
"""psyqapi — superficie de la API PsyQ que usa el codigo del juego (etapa 5).

Cruza la descompilacion completa (DumpAll de Ghidra: data/ghidra/all_*.c) con
las firmas PsyQ de ghidra_psx_ldr (como tools/ghidra/libsplit.py) y lista:

  1. las funciones de libreria a las que llama codigo PROPIO del juego (la API
     que hay que reimplementar), por libreria y con el numero de llamadores;
  2. las operaciones GTE (COP2) que el codigo propio usa en linea (Ghidra las
     muestra como gte_xxx): no pasan por libgte, pero hay que emularlas igual.

Uso:
    python3 tools/psyqapi.py <dir_firmas_psyq> data/ghidra/all_ace_eu.c [overlays...]
"""
import collections
import json
import os
import re
import sys

FUNC_RE = re.compile(r'// ==== (\S+) @ (\S+)\n(.*?)(?=\n// ==== |\Z)', re.S)
CALL_RE = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
GTE_RE = re.compile(r'\bgte_(\w+)\s*\(')


def load_sigs(sigdir):
    sym2lib = {}
    for fn in os.listdir(sigdir):
        if not fn.endswith('.json'):
            continue
        lib = fn[:-5].replace('.LIB', '').replace('.OBJ', '')
        for obj in json.load(open(os.path.join(sigdir, fn))):
            for lab in obj.get('labels', []):
                sym2lib.setdefault(lab['name'], lib)
    return sym2lib


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    sym2lib = load_sigs(argv[1])
    funcs = {}
    for path in argv[2:]:
        for name, addr, body in FUNC_RE.findall(open(path, encoding='latin1').read()):
            funcs.setdefault(name, body)
    lib_names = {n for n in funcs if n in sym2lib}
    callers = collections.defaultdict(set)
    gte = collections.Counter()
    gte_funcs = collections.defaultdict(set)
    for name, body in funcs.items():
        if name in lib_names:
            continue
        for c in set(CALL_RE.findall(body)):
            if c in sym2lib and c != name:
                callers[c].add(name)
        for op in GTE_RE.findall(body):
            gte[op] += 1
            gte_funcs[op].add(name)
    bylib = collections.defaultdict(list)
    for f, cs in callers.items():
        bylib[sym2lib[f]].append((f, len(cs)))
    total = sum(len(v) for v in bylib.values())
    print('API PsyQ llamada por codigo propio: %d funciones' % total)
    for lib in sorted(bylib, key=lambda l: -len(bylib[l])):
        items = sorted(bylib[lib], key=lambda x: (-x[1], x[0]))
        print('  %-8s %3d: %s' % (lib, len(items), ', '.join('%s(%d)' % it for it in items)))
    print('\nOperaciones GTE en linea en codigo propio: %d distintas, %d usos, en %d funciones' % (
        len(gte), sum(gte.values()), len(set().union(*gte_funcs.values())) if gte_funcs else 0))
    print('  ' + ', '.join('%s(%d)' % kv for kv in gte.most_common()))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
