"""ulz — descompresor del formato ULZ de Namco (magic "Ulz\\x1a").

Las secciones de los registros de ACE.BPB vienen comprimidas con este
formato. Por eso una busqueda de cabeceras TIM sobre los registros en crudo
da cero coincidencias: hay que descomprimir antes.

FORMATO (verificado en 659 de 659 secciones ULZ de ACE.BPB europeo):

    offset  tipo   campo
    0x00    char4  magic = "Ulz\\x1a"
    0x04    u32    bits 0-23: tamano descomprimido ; bits 24-31: variante (siempre 2)
    0x08    u32    bits 0-23: offset del flujo de literales ; bits 24-31: nbits
    0x0C    u32    offset del flujo de referencias
    0x10    u32[]  flujo de flags, hasta el offset de literales

Tres flujos independientes, todos relativos al inicio de la seccion:

  - flags: palabras u32 little endian, se consumen del bit 31 al bit 0.
      bit 1 -> copiar un byte del flujo de literales
      bit 0 -> leer un u16 del flujo de referencias:
               distancia = (v & ((1 << nbits) - 1)) + 1
               longitud  = (v >> nbits) + 3
               y copiar `longitud` bytes desde `distancia` atras en la salida
  - literales: bytes sueltos
  - referencias: u16 little endian

Prueba de que la lectura es correcta: tras producir exactamente el tamano
declarado, la holgura de cada flujo es
    flags:        4 bytes siempre (una palabra terminadora)
    literales:    0..3 bytes (relleno de alineacion)
    referencias:  0 o 2 bytes (relleno de alineacion a u32)
en las 659 secciones. Un error en el orden de bits, la polaridad o el reparto
distancia/longitud rompe esa coherencia de inmediato.

La variante (byte alto de 0x04) vale 2 en todas las secciones de primer
nivel. La variante 0 aparece en las 604 entradas ULZ anidadas del registro de
tipo 13 (0072): con este mismo algoritmo salen TIM con cabeceras coherentes,
pero en 413/604 el flujo de literales se queda corto (se piden mas literales
de los que hay entre lit_off y ref_off). La diferencia exacta de la variante 0
esta SIN RESOLVER, asi que el descompresor se niega a tratarla.

Uso:
    python3 tools/ulz.py <archivo> [offset] [salida]   # descomprimir uno
    python3 tools/ulz.py --check <dir>                 # validar todas las secciones ULZ
"""
import os
import struct
import sys

MAGIC = b"Ulz\x1a"


class UlzError(ValueError):
    pass


def header(buf, off=0):
    """Devuelve (size, variant, lit_off, nbits, ref_off) de la cabecera."""
    if buf[off:off + 4] != MAGIC:
        raise UlzError("no es ULZ")
    a, b, c = struct.unpack_from("<III", buf, off + 4)
    return a & 0xFFFFFF, a >> 24, b & 0xFFFFFF, b >> 24, c


def decompress(buf, off=0, end=None, stats=False):
    """Descomprime la seccion ULZ que empieza en `off`.

    Con stats=True devuelve tambien la holgura de cada flujo
    (flags, literales, referencias), util para validar.
    """
    size, variant, lit, nbits, ref = header(buf, off)
    if variant != 2:
        raise UlzError(f"variante {variant} no vista; no se adivina")
    if end is None:
        end = len(buf)
    mask = (1 << nbits) - 1
    out = bytearray()
    fp, lp, rp = off + 16, off + lit, off + ref
    flags = cnt = 0
    while len(out) < size:
        if cnt == 0:
            flags = struct.unpack_from("<I", buf, fp)[0]
            fp += 4
            cnt = 32
        bit = flags >> 31
        flags = (flags << 1) & 0xFFFFFFFF
        cnt -= 1
        if bit:
            out.append(buf[lp])
            lp += 1
        else:
            v = buf[rp] | (buf[rp + 1] << 8)
            rp += 2
            dist = (v & mask) + 1
            length = (v >> nbits) + 3
            if dist > len(out):
                raise UlzError(f"referencia fuera de ventana (dist={dist}, salida={len(out)})")
            for _ in range(length):
                out.append(out[-dist])
    del out[size:]
    if fp > off + lit or lp > off + ref or rp > end:
        raise UlzError("un flujo invade al siguiente: lectura incoherente")
    if stats:
        return bytes(out), (off + lit - fp, off + ref - lp, end - rp)
    return bytes(out)


def _check(directory):
    """Valida el descompresor contra todas las secciones ULZ de los registros."""
    import collections
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from acerec import Record  # noqa: E402
    slack = collections.Counter()
    total = bad = 0
    for name in sorted(os.listdir(directory)):
        rec = Record.from_file(os.path.join(directory, name))
        if rec is None:
            continue
        for k, (a, b) in enumerate(rec.bounds()):
            if rec.raw[a:a + 4] != MAGIC:
                continue
            total += 1
            try:
                _, s = decompress(rec.raw, a, b, stats=True)
                slack[s] += 1
            except UlzError as e:
                bad += 1
                print(f"FALLO {name} seccion {k}: {e}")
    print(f"secciones ULZ: {total}  fallos: {bad}")
    print("holgura (flags, literales, referencias) -> num. secciones:")
    for s, n in sorted(slack.items()):
        print(f"  {s}: {n}")
    ok = all(f == 4 and 0 <= li <= 3 and r in (0, 2) for f, li, r in slack)
    print("coherencia de flujos:", "OK" if ok and not bad else "ROTA")
    return 0 if ok and not bad else 1


def main(argv):
    if len(argv) >= 3 and argv[1] == "--check":
        return _check(argv[2])
    if len(argv) < 2:
        print(__doc__)
        return 2
    buf = open(argv[1], "rb").read()
    off = int(argv[2], 0) if len(argv) > 2 else 0
    data = decompress(buf, off)
    if len(argv) > 3:
        with open(argv[3], "wb") as fh:
            fh.write(data)
    print(f"{len(data)} bytes descomprimidos")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
