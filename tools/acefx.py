#!/usr/bin/env python3
"""acefx — texturas de efectos del registro comun 0000 (explosiones, humo, llamas).

El registro 0000 trae dos TIM de 4 bpp que el juego deja fijos en VRAM:

  s0  64x256 u16 en (448,256), 16 CLUT en (352,256)  -> tpage 0x17
      rejilla 4x4 de celdas de 64 px: explosiones, fuego y humo
  s1  64x256 u16 en (512,256), 12 CLUT en (352,272)  -> tpage 0x18
      llamas de motor y misil, salpicaduras, restos

Uso de cada zona segun el codigo (ver docs/04-formatos.md):

  FUN_80084324  explosion: fotogramas 6..15 de la rejilla de s0, CLUT 0/1
  FUN_8008451C  humo: misma rejilla, CLUT 3 (0x40D6, gris)
  FUN_8008473C  salpicadura/restos: celdas de 32 px de s1, filas v=0/32/64
  FUN_8008EBDC  llama del postquemador: s1, v=96..159 (u=0x60/0xA0/...)

Uso:
    python3 tools/acefx.py sheet data/bpb/0000_*.bin -o data/png/fx
    python3 tools/acefx.py frames data/bpb/0000_*.bin -o data/png/fx

`sheet` escribe cada TIM con todas sus CLUT; `frames` escribe la tira de la
explosion (fotogramas 6-15, CLUT 0) tal como la recorre FUN_80084324.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acerec import Record  # noqa: E402
import tim2png  # noqa: E402

EXPLOSION_FIRST, EXPLOSION_LAST = 6, 15


def load(path):
    rec = Record.from_file(path)
    if rec is None or rec.type is None:
        sys.exit('no es un registro: %s' % path)
    out = []
    for k in (0, 1):
        d = rec.section(k)
        tims = tim2png.scan(d)
        if not tims:
            sys.exit('s%d sin TIM' % k)
        out.append((d, tims[0]))
    return out


def cell(px, w, x0, y0, n):
    """Recorta una celda n x n de una imagen RGBA de ancho w."""
    rows = []
    for y in range(y0, y0 + n):
        rows.append(px[(y * w + x0) * 4:(y * w + x0 + n) * 4])
    return b''.join(rows)


def cmd_sheet(args):
    os.makedirs(args.out, exist_ok=True)
    for k, (d, t) in enumerate(load(args.record)):
        rows = t['clut']['h']
        for row in range(rows):
            w, h, px = tim2png.to_rgba(d, t, row)
            p = os.path.join(args.out, 's%d_clut%02d.png' % (k, row))
            tim2png.write_png(p, w, h, px)
        print('s%d: %s, %d CLUT' % (k, tim2png.describe(t), rows))


def cmd_frames(args):
    os.makedirs(args.out, exist_ok=True)
    (d, t), _ = load(args.record)
    w, h, px = tim2png.to_rgba(d, t, args.clut)
    frames = range(EXPLOSION_FIRST, EXPLOSION_LAST + 1)
    strip = bytearray(64 * len(frames) * 64 * 4)
    sw = 64 * len(frames)
    for i, f in enumerate(frames):
        c = cell(px, w, (f & 3) * 64, (f >> 2) * 64, 64)
        for y in range(64):
            o = (y * sw + i * 64) * 4
            strip[o:o + 256] = c[y * 256:(y + 1) * 256]
    p = os.path.join(args.out, 'explosion_clut%d.png' % args.clut)
    tim2png.write_png(p, sw, 64, bytes(strip))
    print('%s: fotogramas %d-%d' % (p, EXPLOSION_FIRST, EXPLOSION_LAST))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name, fn in (('sheet', cmd_sheet), ('frames', cmd_frames)):
        s = sub.add_parser(name)
        s.add_argument('record')
        s.add_argument('-o', '--out', default='data/png/fx')
        s.set_defaults(fn=fn)
        if name == 'frames':
            s.add_argument('--clut', type=int, default=0)
    args = ap.parse_args()
    args.fn(args)


if __name__ == '__main__':
    main()
