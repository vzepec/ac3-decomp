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
  clase 1: instantanea de entidad (FUN_8007C6DC), 4 palabras: entidad =
           bits 9-14; palabras 1-3 = x, y, z (20 bits altos, coma fija << 12)
           y angulos rx, ry, rz en los 12 bits bajos (angulo >> 2; 0x4000 = 360)
  clase 2: entidades (FUN_8007CC34: 6 palabras = posicion + angulos; 4; 1)
  clase 3: efectos (FUN_8007D66C puntual, FUN_8007D720 con velocidad,
           FUN_8007D830 estela, FUN_8007D940 global)
  clase 4: FUN_8007CEB4 (2 palabras), SIN RESOLVER

Uso:
    python3 tools/acedemo.py data/bpb/0111_*.bin [mas demos]
    python3 tools/acedemo.py --track <entidad> data/bpb/0111_*.bin [cada_n]
    python3 tools/acedemo.py --heading data/bpb/0111_*.bin   # unidad de angulos
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


def snapshots(ev_words):
    """[(fotograma, entidad, (x, y, z), (rx, ry, rz))] de las instantaneas de clase 1."""
    out = []
    frame = 0
    for words in ev_words:
        w = words[0]
        if w == 1:
            frame += 1
        elif (w >> 4) & 7 == 1 and (w & 0xF) == 4 and ((w >> 7) & 3) == 0:
            ent = (w >> 9) & 0x3F
            pos = tuple((v >> 12) for v in (struct.unpack('<i', struct.pack('<I', x))[0] for x in words[1:4]))
            ang = tuple(((x << 20) & 0xFFFFFFFF) for x in words[1:4])
            ang = tuple((struct.unpack('<i', struct.pack('<I', a))[0] >> 18) & 0x3FFF for a in ang)
            out.append((frame, ent, pos, ang))
    return out


def walk_words(data, start, end, stop):
    """Como walk(), pero devuelve las palabras de cada evento."""
    pos, out = start, []
    while True:
        w = struct.unpack_from('<I', data, pos)[0]
        size = w & 0xF
        if size == 0:
            return out
        out.append(struct.unpack_from('<%dI' % size, data, pos))
        pos += 4 * size
        if pos >= end:
            pos = 0
        if pos == stop or len(out) > 1000000:
            return out


def track(path, ent, every):
    head, data = blocks(path)
    rd, oldest = struct.unpack_from('<ii', head, 0xC)
    length = min((struct.unpack_from('<I', head, 0x2050)[0] & ~3) - 0x40, len(data))
    snaps = [s for s in snapshots(walk_words(data, oldest if oldest >= 0 else 0, length, rd)) if s[1] == ent]
    prev = None
    jumps = []
    for f, e, p, a in snaps:
        if prev:
            jumps.append(max(abs(p[k] - prev[k]) for k in range(3)))
        prev = p
    print('%s: entidad %d, %d instantaneas' % (os.path.basename(path), ent, len(snaps)))
    for f, e, p, a in snaps[::every]:
        print('  fotograma %5d  pos (%7d, %6d, %7d)  angulos (%5d, %5d, %5d) = rumbo %.0f grados' % (
            f, p[0], p[1], p[2], a[0], a[1], a[2], a[1] * 360.0 / 0x4000))
    if jumps:
        jumps.sort()
        print('  salto entre instantaneas: mediana %d, p99 %d, maximo %d' % (
            jumps[len(jumps) // 2], jumps[99 * len(jumps) // 100], jumps[-1]))


def heading_check(path):
    """Valida la unidad y el sentido de los angulos: compara el rumbo grabado (ry)
    con la direccion del movimiento del jugador entre instantaneas consecutivas."""
    import math
    head, data = blocks(path)
    rd, oldest = struct.unpack_from('<ii', head, 0xC)
    length = min((struct.unpack_from('<I', head, 0x2050)[0] & ~3) - 0x40, len(data))
    sn = [x for x in snapshots(walk_words(data, oldest if oldest >= 0 else 0, length, rd)) if x[1] == 0]
    for unit in (0x4000, 0x1000, 0x10000):
        errs = []
        for (_, _, p0, a0), (_, _, p1, _) in zip(sn, sn[1:]):
            dx, dz = p1[0] - p0[0], p1[2] - p0[2]
            if math.hypot(dx, dz) < 10:
                continue
            mov = math.atan2(dx, dz)                    # PS1: adelante = (sin, cos)
            hd = a0[1] * 2 * math.pi / unit
            errs.append(math.degrees(abs((mov - hd + math.pi) % (2 * math.pi) - math.pi)))
        errs.sort()
        print('%s: unidad %#x -> error mediano rumbo/movimiento %.1f grados' % (
            os.path.basename(path), unit, errs[len(errs) // 2]))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == '--heading':
        for path in sys.argv[2:]:
            heading_check(path)
        return
    if sys.argv[1] == '--track':
        track(sys.argv[3], int(sys.argv[2]), int(sys.argv[4]) if len(sys.argv) > 4 else 300)
        return
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
