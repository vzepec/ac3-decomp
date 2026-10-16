#!/usr/bin/env python3
"""acemusic — pista de musica (entrada del ACE.SPH) de cada mision, leida del ACE.BIN.

Codigo (EU): al arrancar una mision, FUN_8007DDCC encola la musica (FUN_8002C4B0, orden
de tipo 3 de la cola del CD, que reproduce FUN_8002BABC con spb_stream_start en modo XA).
  - indice de mision i = g_state+0x390 (el mismo que da el registro: tabla 0x800AF748,
    u16, registro = 6 + i);
  - si g_state+0x3A9 != 0 (DEMOSTRACION: lo pone a 1/2 el contador de demos en
    FUN_800563C8): descriptor = punteros 0x800B049C[ bytes 0x800B0540[i] ];
  - si vale 0 (partida): descriptor fijo 0x800B21B4 (recurso 0x822); despues el guion de
    la mision cambia la pista con FUN_800550F0(indice), sobre la misma tabla de
    descriptores. Que indice pide cada guion: SIN RESOLVER.
Descriptor (12 bytes): s16 recurso (0x800 | entrada del SPH), s16 fundido, s16 tiempo,
s16 desplazamiento de inicio, u32 siguiente (apunta a si mismo: la pista se repite).

Uso:
    python3 tools/acemusic.py data/exe/ace_eu.bin [--c]   # --c: tabla para android/src/music.c
"""
import struct
import sys

BASE = 0x80010000


def table(path):
    d = open(path, 'rb').read()
    o = lambda a: a - BASE  # noqa: E731
    recs = struct.unpack_from('<41H', d, o(0x800AF748))
    idx = d[o(0x800B0540):o(0x800B0540) + 72]
    out = []
    for i, rec in enumerate(recs):
        ptr = struct.unpack_from('<I', d, o(0x800B049C) + 4 * idx[i])[0]
        res, fade, t, off, nxt = struct.unpack_from('<4hI', d, o(ptr))
        out.append((rec, idx[i], res & 0xFFFF, off, nxt == ptr))
    default = struct.unpack_from('<h', d, o(0x800B21B4))[0] & 0xFFFF
    return out, default


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out, default = table(sys.argv[1])
    if '--c' in sys.argv:
        print('/* entrada del ACE.SPH por registro de mision 6..46 (tools/acemusic.py --c) */')
        print('static const uint8_t MISSION_TRACK[41] = {' + ', '.join(str(r & 0x7FF) for _, _, r, _, _ in out) + '};')
        return 0
    print('pista de partida al arrancar: recurso %#x (entrada %d del SPH)' % (default, default & 0x7FF))
    print('registro  indice  recurso  entrada_SPH  inicio  bucle   (tabla de demostraciones)')
    for rec, i, res, off, loop in out:
        print('%8d  %6d  %#7x  %11d  %6d  %s' % (rec, i, res, res & 0x7FF, off, 'si' if loop else 'no'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
