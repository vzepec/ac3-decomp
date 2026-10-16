#!/usr/bin/env python3
"""psyqcheck — compara funciones de psx/ con el codigo original de ACE.BIN.

Ejecuta cada funcion PsyQ original en el interprete tools/mipsemu.py (el
oraculo) y la reimplementacion de psx/ (psx/build/test_math) sobre las mismas
entradas, y cuenta las diferencias. Una funcion se da por buena solo con 0
diferencias.

Uso:
    python3 tools/psyqcheck.py data/exe/ace_eu.bin [--rapido]
"""
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsemu import Machine  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# direcciones en ACE.BIN EU (data/ghidra/all_ace_eu.c)
ADDR = {'rsin': 0x800A45C8, 'rcos': 0x800A4698, 'SquareRoot0': 0x800A47C8,
        'ratan2': 0x800A6088, 'rand': 0x800AC178, 'srand': 0x800AC1A8}


def build():
    out = os.path.join(ROOT, 'psx', 'build', 'test_math')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    src = [os.path.join(ROOT, 'psx', p) for p in ('test/test_math.c', 'src/libgte_math.c', 'src/libc_psx.c')]
    subprocess.check_call(['cc', '-O2', '-Wall', '-Wextra', '-I' + os.path.join(ROOT, 'psx', 'include')] + src
                          + ['-lm', '-o', out])
    return out


def cases(fast):
    rnd = random.Random(1234)
    n = 1 if fast else 4
    c = [('rsin', a, 0) for a in range(-4200, 4200 * n, 1 if not fast else 7)]
    c += [('rcos', a, 0) for a in range(-4200, 4200 * n, 1 if not fast else 7)]
    sq = list(range(0, 5000)) + [rnd.randrange(0, 1 << 31) for _ in range(3000 * n)] + [(1 << k) + d for k in range(31) for d in (-1, 0, 1)]
    c += [('SquareRoot0', v, 0) for v in sq if 0 <= v < (1 << 31)]
    pts = [(rnd.randrange(-1 << 20, 1 << 20), rnd.randrange(-1 << 20, 1 << 20)) for _ in range(3000 * n)]
    pts += [(rnd.randrange(-1 << 30, 1 << 30), rnd.randrange(-1 << 30, 1 << 30)) for _ in range(1000 * n)]
    pts += [(y, x) for y in range(-40, 41, 5) for x in range(-40, 41, 5)]
    c += [('ratan2', y, x) for y, x in pts]
    c += [('srand', 12345, 0)] + [('rand', 0, 0)] * (2000 * n) + [('srand', 1, 0)] + [('rand', 0, 0)] * 500
    return c


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    acebin = sys.argv[1]
    fast = '--rapido' in sys.argv
    exe = build()
    cs = cases(fast)
    inp = ''.join('%s %d %d\n' % c for c in cs)
    mine = subprocess.run([exe, acebin], input=inp, capture_output=True, text=True, check=True)
    sys.stderr.write(mine.stderr)
    got = [int(x) for x in mine.stdout.split()]
    m = Machine.from_acebin(acebin)
    stats = {}
    for (fn, a, b), g in zip(cs, got):
        ref = m.call(ADDR[fn], [a, b])
        if fn == 'srand':
            ref = g = 0
        st = stats.setdefault(fn, [0, 0, []])
        st[0] += 1
        if ref != g:
            st[1] += 1
            if len(st[2]) < 5:
                st[2].append((a, b, ref, g))
    ok = True
    for fn, (n, bad, ex) in stats.items():
        if fn == 'srand':
            continue
        print('%-12s %6d casos, %d diferencias%s' % (fn, n, bad, ('  ej. (a, b, original, psx): %s' % ex) if bad else ''))
        ok &= bad == 0
    print('TODO IDENTICO' if ok else 'HAY DIFERENCIAS')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
