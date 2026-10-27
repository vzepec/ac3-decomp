#!/usr/bin/env python3
"""entidades — la tabla de entidades, el contexto del jugador y los mandos en vuelo (etapa 9, paso 2).

Vuelca la RAM de la version americana en un VBlank de una mision (binario libre con AC3_DUMPMEM; el
guion de mando de tools/misiones.py) y muestra:
- la tabla de 64 entidades (*0x800BB824: punteros; 0-31 aviones, la 0 el jugador; 32-63 objetos
  de tierra): banderas, modelo, tipo (+0x48), vida (+0x4e), posicion (+8..+0x10, >> 12);
- el contexto del jugador: entidad 0, DAT_800BBA74 (control, 0x70 bytes) y DAT_800BBA78 (estado,
  0x3c), la entrada del juego (g_state + 0x120) y sus ejes;
- las 8 ranuras de mando del juego (2 puertos x multitap; salida en g_state + 0x50 + k * 0x1a).
Con --todas, solo el resumen de cada una de las 41 misiones (que ranuras de avion estan activas).

Uso:
    python3 tools/entidades.py <imagen_US> [--mision N] [--vblank V] [--todas] [--salida dir]
Las direcciones son de la US (docs/15-etapa9.md, paso 2).
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import misiones  # noqa: E402

ENT_TABLE = 0x800BB824   # puntero a la tabla de 64 punteros a entidad
CTL_A = 0x800BBA74       # control del jugador (0x70 bytes)
CTL_B = 0x800BBA78       # estado del jugador (0x3c bytes: HUD, camara)
G_RENDER = 0x800BB81C
G_STATE = 0x800BB828
PAD_RAW = 0x800BB464     # puntero al bufer del mando (0x800BED38)


def dump(disc, mission, vblank, out):
    path = os.path.join(out, 'ram_m%d_v%d.bin' % (mission, vblank))
    env = dict(os.environ, AC3_PAD_ID='73', AC3_MISSION=str(mission),
               AC3_DUMPMEM='%d,80000000,200000,%s' % (vblank, path))
    subprocess.call([os.path.join(ROOT, 'psx', 'build', 'ac3_libre'), disc, '-', str(vblank + 1), out, '0',
                     misiones.GUION], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT)
    return open(path, 'rb').read()


class Ram:
    def __init__(self, m):
        self.m = m

    def w(self, a):
        return struct.unpack_from('<I', self.m, a & 0x1FFFFF)[0]

    def i(self, a):
        return struct.unpack_from('<i', self.m, a & 0x1FFFFF)[0]

    def h(self, a):
        return struct.unpack_from('<h', self.m, a & 0x1FFFFF)[0]

    def b(self, a):
        return self.m[a & 0x1FFFFF]


def entities(r):
    t = r.w(ENT_TABLE)
    return [r.w(t + 4 * k) for k in range(64)]


def active(r, e):
    return r.w(e) & 0xC000 == 0xC000 or r.w(e) & 0xFFFFFFC0 != 0


def show(r):
    gr, st = r.w(G_RENDER), r.w(G_STATE)
    ents = entities(r)
    print('g_render %08x  g_state %08x  tabla de entidades %08x' % (gr, st, r.w(ENT_TABLE)))
    print(' k  entidad   banderas  mod tipo  vida  posicion (>> 12)')
    for k, e in enumerate(ents):
        if not active(r, e):
            continue
        print('%2d  %08x  %08x  %3d  %02x  %5d  %s' % (k, e, r.w(e), r.b(e + 4), r.b(e + 0x48), r.h(e + 0x4E),
                                                    [r.i(e + o) >> 12 for o in (8, 12, 16)]))
    libres = [k for k in range(1, 32) if not active(r, ents[k])]
    print('ranuras de avion libres:', libres)
    e0 = ents[0]
    print('jugador: entidad %08x (g_render%+#x), angulos %s, velocidad %s, vida %d' % (
        e0, e0 - gr, [r.h(e0 + o) for o in (0x18, 0x1A, 0x1C)], [r.i(e0 + o) for o in (0x38, 0x3C, 0x40)],
        r.h(e0 + 0x4E)))
    print('control (DAT_800BBA74) %08x, estado (DAT_800BBA78) %08x' % (r.w(CTL_A), r.w(CTL_B)))
    inp = st + 0x120
    print('entrada del juego %08x: clase %d, acciones %08x, ejes %s' % (
        inp, r.w(inp + 4), r.w(inp + 8), [r.i(inp + 0x1C + 4 * k) for k in range(6)]))
    raw = r.w(PAD_RAW)
    for k in range(8):
        d = raw + 0x44 + k * 0x10
        o = r.w(d + 4)
        print('mando, ranura %d (puerto %02x): salida %08x tipo %02x botones %04x ejes %s' % (
            k, r.b(d), o, r.b(o + 0xC), struct.unpack_from('<H', r.m, (o + 0xE) & 0x1FFFFF)[0],
            [r.h(o + x) for x in (0x12, 0x14, 0x16, 0x18)]))


def main():
    opts = sys.argv[1:]
    if not opts or opts[0].startswith('--'):
        sys.exit(__doc__)
    disc = opts[0]

    def opt(name, default):
        return opts[opts.index(name) + 1] if name in opts else default
    mission, vblank = int(opt('--mision', 0)), int(opt('--vblank', 3200))
    out = opt('--salida', None) or tempfile.mkdtemp(prefix='entidades_')
    os.makedirs(out, exist_ok=True)
    if '--todas' in opts:
        for m in range(41):
            r = Ram(dump(disc, m, vblank, out))
            ents = entities(r)
            air = [k for k in range(32) if active(r, ents[k])]
            gnd = sum(active(r, ents[k]) for k in range(32, 64))
            print('mision %2d: aviones %s, objetos de tierra %d, ranura 1 %s' % (
                m, air, gnd, 'ocupada' if 1 in air else 'libre'), flush=True)
        return 0
    show(Ram(dump(disc, mission, vblank, out)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
