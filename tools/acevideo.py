#!/usr/bin/env python3
"""acevideo — modo de video y ritmo de ticks de ACE.BIN (EU y US).

En mision, la inicializacion (FUN_80032514 en EU) pone el intervalo de VSync
del bucle principal (ac3_main, 0x80025D08: VSync(*(DAT_800BACB8 + 0x14))) a 2:
un tick de juego cada 2 refrescos verticales. El ritmo en segundos depende
del modo de video, que se deduce de la altura de pantalla pasada a
SetDefDispEnv: 256 lineas = PAL (50 Hz), 240 = NTSC (60 Hz).

Patron (validado con la version europea, que llama a SetVideoMode(1) = PAL):
  addiu s1, zero, ALTO ... addiu a3, zero, 0x140 ; jal SetDefDispEnv ; sw s1, 16(sp)

Uso:
    python3 tools/acevideo.py data/exe/ace_eu.bin data/exe/ace_us.exe
"""
import struct
import sys


def load(path):
    d = open(path, 'rb').read()
    if d[:8] == b'PS-X EXE':
        return d[2048:], struct.unpack_from('<I', d, 0x18)[0]
    return d, 0x80010000


def heights(path):
    d, base = load(path)
    w = struct.unpack('<%dI' % (len(d) // 4), d[:len(d) // 4 * 4])
    out = []
    for i in range(len(w) - 2):
        if w[i] == 0x24070140 and (w[i + 1] >> 26) == 3 and w[i + 2] == 0xAFB10010:
            for k in range(i - 1, max(i - 40, 0), -1):
                if (w[k] >> 16) == 0x2411:
                    out.append((base + 4 * i, w[k] & 0xFFFF))
                    break
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        hs = heights(path)
        lines = sorted({h for _, h in hs})
        mode = 'PAL (50 Hz)' if 256 in lines else 'NTSC (60 Hz)' if 240 in lines else 'desconocido'
        hz = 50 if 256 in lines else 60 if 240 in lines else 0
        print('%s: alturas %s -> %s; con VSync(2): %s ticks/s' % (
            path, lines, mode, hz // 2 if hz else '?'))


if __name__ == '__main__':
    main()
