#!/usr/bin/env python3
"""acedemo — grabaciones de demo (registros de tipo 1, entradas 111-126).

Una demo es la grabacion de la cola de eventos del juego (FUN_8007C8AC), la
misma que alimenta efectos y entidades. El registro trae un unico bloque
(preludio) con una tabla de offsets de 2 entradas:

  bloque 0 (0x2054 B): estructura del grabador/reproductor (DAT_800BAF40)
      +0x00  numero de marcas de fotograma grabadas
      +0x0C  posicion de lectura (relativa al bloque 1)
      +0x10  posicion del evento mas antiguo (relativa; -1 = sin usar)
      +0x2050 longitud de los datos; la vuelta del bufer esta en longitud - 0x40
  bloque 1: eventos, bufer circular de palabras de 32 bits

Evento: palabra 0 -> tamano = bits 0-3 (en palabras), clase = bits 4-6
(FUN_8007C778). Una palabra igual a 1 es una marca de fotograma.
  clase 0: marca de fotograma y estado del jugador (FUN_8007C948, subtipo bits 7-9)
  clase 2: entidades (FUN_8007CC34: 6 palabras = posicion + angulos; 4; 1)
  clase 3: efectos (FUN_8007D66C puntual, FUN_8007D720 con velocidad,
           FUN_8007D830 estela, FUN_8007D940 global)
  clase 4: FUN_8007CEB4 (2 palabras), SIN RESOLVER

Uso:
    python3 tools/acedemo.py data/bpb/0111_*.bin [mas demos]
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acerec import Record  # noqa: E402


def blocks(path):
    raw = open(path, 'rb').read()
    rec = Record.from_file(path)
    a, b = rec.prelude_bounds()
    p = raw[a:b]
    n, o0, o1 = struct.unpack_from('<3I', p, 0)
    if n != 2:
        raise ValueError('no es una demo')
    return p[o0:o1], p[o1:]


def walk(data, start, end, stop):
    """Recorre el bufer circular como el lector del juego (FUN_8007C7E8): avanza
    evento a evento y, si queda en `end` o mas alla, vuelve a 0 (el grabador
    escribe el evento entero aunque pase de `end`: para eso hay 0x40 B de margen).
    Para al llegar a `stop` (posicion de escritura). Devuelve (eventos, error)."""
    pos = start
    out = []
    while True:
        if pos + 4 > len(data):
            return out, 'fuera del bloque en %d' % pos
        w = struct.unpack_from('<I', data, pos)[0]
        size = w & 0xF
        if size == 0:
            return out, 'tamano 0 en %d' % pos
        out.append((w, (w >> 4) & 7, size))
        pos += 4 * size
        if pos >= end:
            pos = 0
        if pos == stop:
            return out, None
        if len(out) > 1000000:
            return out, 'sin fin'


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        head, data = blocks(path)
        frames, rd, oldest = struct.unpack_from('<I', head, 0)[0], *struct.unpack_from('<ii', head, 0xC)
        # el reproductor fija el final en inicio + (longitud & ~3) - 0x40 (FUN_8007C548)
        length = (struct.unpack_from('<I', head, 0x2050)[0] & ~3) - 0x40
        length = min(length, len(data))
        start = oldest if oldest >= 0 else 0
        ev, err = walk(data, start, length, rd)
        marks = sum(1 for w, c, s in ev if w == 1)
        kinds = collections.Counter((c, s) for w, c, s in ev)
        print('%s: fotogramas en cabecera %d, marcas contadas %d%s' % (
            os.path.basename(path), frames, marks, ('  [' + err + ']') if err else ''))
        print('   eventos por (clase, tamano):', dict(sorted(kinds.items())))


if __name__ == '__main__':
    main()
