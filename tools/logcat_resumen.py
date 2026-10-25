#!/usr/bin/env python3
"""logcat_resumen — resumen del registro de AC3Game en vuelo (etapa 8, paso 5).

De cada segundo con el juego en marcha (30 pasos o mas) y dibujo de la GPU: fps, vueltas de
presentacion de mas de 20 ms (una imagen que se queda un refresco mas), la vuelta mas larga y el
tiempo de CPU en el dibujo y en eglSwapBuffers.

Uso:
    python3 tools/logcat_resumen.py <registro> [<registro> ...]
"""
import re
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        rows = {}
        for line in open(path, errors='replace'):
            m = re.match(r'\S+ (\d\d:\d\d:\d\d)\.\d+ .*AC3Game *: (.*)', line)
            if not m:
                continue
            t, msg = m.groups()
            r = rows.setdefault(t, {})
            a = re.search(r'fps (\d+).*pasos (\d+)', msg)
            if a:
                r.update(fps=int(a[1]), pasos=int(a[2]))
            a = re.search(r'GPU x\d: (\d+) imagenes, ([\d.]+) ms de dibujo y ([\d.]+) de eglSwap.*; (\d+) llamadas', msg)
            if a:
                r.update(dib=float(a[2]), swap=float(a[3]), llam=int(a[4]))
            a = re.search(r'presentacion: (\d+) vueltas, la mas larga ([\d.]+) ms, (\d+) de mas de 20 ms', msg)
            if a:
                r.update(larga=float(a[2]), lentas=int(a[3]))
        sel = [r for r in rows.values() if r.get('pasos', 0) >= 25 and r.get('llam', 0) > 30 and 'larga' in r]
        if not sel:
            print(path, 'sin datos de vuelo')
            continue

        def av(k):
            return sum(r[k] for r in sel) / len(sel)
        print('%s: %d s en vuelo; fps %.2f (min %d); vueltas de mas de 20 ms: %d (%.2f por s); la mas larga: '
              'media %.1f, max %.1f ms; CPU dibujo %.2f ms, swap %.2f ms' % (
                  path, len(sel), av('fps'), min(r['fps'] for r in sel), sum(r['lentas'] for r in sel), av('lentas'),
                  av('larga'), max(r['larga'] for r in sel), av('dib'), av('swap')))


if __name__ == '__main__':
    main()
