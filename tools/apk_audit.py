#!/usr/bin/env python3
"""apk_audit — comprueba que un APK no lleva bytes del juego (etapa 7).

Busca en todos los archivos del APK (zip: bibliotecas nativas, dex, recursos) cualquier
ventana de 32 bytes alineada a 4 que aparezca en los binarios del juego dados (ACE.BIN y
overlays descomprimidos: data/exe/ace_*.bin, data/exe/ovl*/overlay*.exe). Se ignoran las
ventanas "triviales" del juego (menos de 6 bytes distintos: ceros, rellenos, tablas
uniformes), que pueden coincidir por casualidad con cualquier binario.
Sirve para el APK libre: debe dar 0 coincidencias. Caso positivo conocido: el APK con el codigo
traducido incrusta ACE.BIN (.incbin) y debe dar miles.

Uso:
    python3 tools/apk_audit.py <apk> <binario_del_juego>...
Devuelve 0 si no hay coincidencias.
"""
import struct
import sys
import zipfile

W = 32


def windows(data):
    out = set()
    for o in range(0, len(data) - W + 1, 4):
        w = data[o:o + W]
        if len(set(w)) >= 6:
            out.add(w)
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    game = set()
    for p in sys.argv[2:]:
        d = open(p, 'rb').read()
        if d[:8] == b'PS-X EXE':
            d = d[2048:]
        game |= windows(d)
    print('%d ventanas de %d bytes del juego' % (len(game), W))
    total = 0
    with zipfile.ZipFile(sys.argv[1]) as z:
        for info in z.infolist():
            d = z.read(info)
            hits = 0
            # el binario incrustado puede no estar alineado a 4 respecto al archivo: los 4 desfases
            for s in range(4):
                for o in range(s, len(d) - W + 1, 4):
                    if d[o:o + W] in game:
                        hits += 1
            if hits:
                print('  %-50s %8d bytes: %d coincidencias' % (info.filename, len(d), hits))
            total += hits
    print('total: %d coincidencias -> %s' % (total, 'SIN BYTES DEL JUEGO' if total == 0 else 'LLEVA BYTES DEL JUEGO'))
    return 0 if total == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
