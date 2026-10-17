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

VARIANTES (byte alto de 0x04), segun el descompresor de ACE.BIN
(FUN_800AE118 en la version EU, decompilado con Ghidra):

  2  la descrita arriba: 32 flags por palabra, se para al llegar al tamano.
     Todas las secciones de primer nivel de ACE.BPB.
  0  mismos flujos y misma codificacion de referencias, pero cada palabra de
     flags lleva un BIT CENTINELA: se consumen bits desde el 31 mientras lo
     que queda tras desplazar no sea 0 (el 1 mas bajo no es un token), y una
     palabra de flags 0 TERMINA el flujo. Las 604 entradas anidadas del
     registro de tipo 13. Validado: tamano exacto y flujos coherentes en
     todas (ver --check).
  1  igual que 0 pero en unidades de 16 bits (literales u16; longitud =
     (v >> (nbits-1)) + 2 halfwords; distancia = (v & mask) * 2 + 2 bytes).
     Implementada segun el codigo del juego, pero NO aparece en los datos:
     sin validar.

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
    if variant not in (0, 1, 2):
        raise UlzError(f"variante {variant} no vista; no se adivina")
    if end is None:
        end = len(buf)
    if variant == 2:
        out, fp, lp, rp = _decode_v2(buf, off, size, lit, nbits, ref)
    else:
        out, fp, lp, rp = _decode_sentinel(buf, off, lit, nbits, ref, wide=(variant == 1))
        if len(out) != size:
            raise UlzError(f"tamano incoherente: {len(out)} != {size}")
    if fp > off + lit or lp > off + ref or rp > end:
        raise UlzError("un flujo invade al siguiente: lectura incoherente")
    if stats:
        return bytes(out), (off + lit - fp, off + ref - lp, end - rp)
    return bytes(out)


def _copy(out, dist, length):
    if dist > len(out):
        raise UlzError(f"referencia fuera de ventana (dist={dist}, salida={len(out)})")
    for _ in range(length):
        out.append(out[-dist])


def _decode_v2(buf, off, size, lit, nbits, ref):
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
            _copy(out, (v & mask) + 1, (v >> nbits) + 3)
    del out[size:]
    return out, fp, lp, rp


def _decode_sentinel(buf, off, lit, nbits, ref, wide):
    """Variantes 0 (bytes) y 1 (halfwords): flags con bit centinela."""
    if wide:
        nbits -= 1
    mask = (1 << nbits) - 1
    out = bytearray()
    fp, lp, rp = off + 16, off + lit, off + ref
    while True:
        flags = struct.unpack_from("<I", buf, fp)[0]
        fp += 4
        if flags == 0:
            break
        while True:
            bit = flags >> 31
            flags = (flags << 1) & 0xFFFFFFFF
            if flags == 0:
                break
            if bit:
                n = 2 if wide else 1
                out += buf[lp:lp + n]
                lp += n
            else:
                v = buf[rp] | (buf[rp + 1] << 8)
                rp += 2
                if wide:
                    _copy(out, (v & mask) * 2 + 2, ((v >> nbits) + 2) * 2)
                else:
                    _copy(out, (v & mask) + 1, (v >> nbits) + 3)
    return out, fp, lp, rp


def _check(directory):
    """Valida el descompresor contra todos los ULZ de los registros.

    Primer nivel: secciones que son ULZ (variante 2). Anidados: entradas de
    las tablas de offsets de secciones en claro (variante 0, registro 0072).
    Holgura esperada: variante 2 -> flags 4 (palabra terminadora sin leer),
    literales 0..3, referencias 0 o 2; variante 0 -> flags 0 (la palabra 0
    terminadora SI se lee), literales 0..3, referencias 0 o 2.
    """
    import collections
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from acerec import Record  # noqa: E402
    from acemodel import offset_table  # noqa: E402
    slack = collections.Counter()
    total = bad = 0

    def one(buf, a, b, where):
        nonlocal total, bad
        total += 1
        variant = buf[a + 7]
        try:
            _, s = decompress(buf, a, b, stats=True)
            slack[(variant,) + s] += 1
        except (UlzError, IndexError, struct.error) as e:
            bad += 1
            print(f"FALLO {where}: {e}")

    for name in sorted(os.listdir(directory)):
        rec = Record.from_file(os.path.join(directory, name))
        if rec is None:
            continue
        for k, (a, b) in enumerate(rec.bounds()):
            if rec.raw[a:a + 4] == MAGIC:
                one(rec.raw, a, b, f"{name} seccion {k}")
                continue
            sec = rec.raw[a:b]
            for j, (c, d) in enumerate(offset_table(sec)):
                if sec[c:c + 4] == MAGIC:
                    one(sec, c, d, f"{name} seccion {k} entrada {j}")
    print(f"ULZ: {total}  fallos: {bad}")
    print("(variante, holgura flags, literales, referencias) -> num.:")
    for s, n in sorted(slack.items()):
        print(f"  {s}: {n}")
    expected_flags = {2: 4, 0: 0}
    ok = all(expected_flags.get(v) == f and 0 <= li <= 3 and r in (0, 2) for v, f, li, r in slack)
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
