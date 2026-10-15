"""acebin — descompresor de ACE.BIN, el modulo principal del juego.

ACE.BIN no es un PS-EXE: lleva una cabecera propietaria de Namco (magic
"PS-Y4a\\xe3w") y el codigo comprimido con un LZ propio.

CABECERA (0x5C bytes; verificada en EU y US):

    0x00  char[8]  magic "PS-Y4a\\xe3w"
    0x08  u32      direccion final = carga + tamano descomprimido
    0x0C  u32      tamano util del archivo (EU: exacto; US: 2 bytes menos)
    0x10  u32      tamano descomprimido
    0x14  u32      0x5C = tamano de la cabecera
    0x1C  u32      punto de entrada
    0x24  u32      direccion de carga (0x80010000)
    0x28  u32      tamano descomprimido (repetido)
    0x3C  u32      pila
    0x58  u32      SUMA de todas las palabras u32 de la salida (mod 2^32)
    0x5C  u8       0x0C en EU y US                       SIN RESOLVER
    0x5D  ...      flujo comprimido

FLUJO COMPRIMIDO (un solo flujo, a diferencia de ULZ):

    repetir:
        u8 flags                 # se usan los bits 0..6 (7 elementos por
                                 # byte); el bit 7 vale 1 en todos los vistos
        para k en 0..6:
            si flags bit k == 1: copiar 1 byte literal
            si no: leer b0, b1
                longitud  = b0 >> 4, y 0 significa 16      (1..16)
                distancia = b1 | (b0 & 0x0F) << 8, y 0 significa 4096
                copiar `longitud` bytes desde `distancia` atras en la salida
    hasta producir el tamano descomprimido

Pruebas:
  - la salida mide exactamente el tamano declarado y se consume toda la
    entrada salvo 1 (EU) o 3 (US) bytes de relleno
  - la suma u32 de la salida coincide EXACTAMENTE con el campo 0x58 en los
    dos discos (con "distancia 0 = rellenar con ceros" no coincide)
  - el punto de entrada cae tras un "jr ra; nop" y empieza con el bucle de
    limpieza de BSS tipico de PsyQ
  - fraccion de jal que apuntan a un prologo: 51,7 %, frente a 53,7 % en
    SCES_020.66 y 54,3 % en ACE.EXE (en claro)

La salida es codigo del juego: va a data/, que no se versiona.

Uso:
    python3 tools/acebin.py <imagen.bin> <salida.bin>          # binario plano
    python3 tools/acebin.py <imagen.bin> <salida.exe> --psexe  # con cabecera PS-X EXE
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psxiso import Disc  # noqa: E402

MAGIC = b"PS-Y"
HEADER_SIZE = 0x5C


class AceBin:
    def __init__(self, raw):
        if raw[:4] != MAGIC:
            raise ValueError("no es ACE.BIN (magic distinto)")
        (self.end_addr, self.file_size, self.size, self.header_size) = struct.unpack_from("<4I", raw, 0x08)
        self.entry = struct.unpack_from("<I", raw, 0x1C)[0]
        self.load_addr = struct.unpack_from("<I", raw, 0x24)[0]
        self.stack = struct.unpack_from("<I", raw, 0x3C)[0]
        self.checksum = struct.unpack_from("<I", raw, 0x58)[0]
        if self.header_size != HEADER_SIZE or not len(raw) - 3 <= self.file_size <= len(raw):
            raise ValueError("cabecera incoherente")
        if self.load_addr + self.size != self.end_addr:
            raise ValueError("carga + tamano != direccion final")
        self.raw = raw

    def decompress(self):
        """Devuelve (datos, bytes_de_entrada_sin_consumir)."""
        b = self.raw
        size = self.size
        out = bytearray()
        i = HEADER_SIZE + 1
        while len(out) < size:
            flags = b[i]
            i += 1
            for k in range(7):
                if len(out) >= size:
                    break
                if (flags >> k) & 1:
                    out.append(b[i])
                    i += 1
                else:
                    b0, b1 = b[i], b[i + 1]
                    i += 2
                    length = (b0 >> 4) or 16
                    dist = (b1 | (b0 & 0x0F) << 8) or 4096
                    if dist > len(out):
                        raise ValueError(f"referencia antes del inicio en la salida {len(out)}")
                    for _ in range(length):
                        out.append(out[-dist])
        if len(out) != size:
            raise ValueError("tamano de salida incorrecto")
        return bytes(out), len(b) - i

    @staticmethod
    def word_sum(data):
        n = len(data) // 4
        return sum(struct.unpack(f"<{n}I", data[:4 * n])) & 0xFFFFFFFF


def psexe_wrap(data, load_addr, entry, stack):
    """Envuelve el binario en una cabecera PS-X EXE estandar de 2048 bytes."""
    hdr = bytearray(2048)
    hdr[0:8] = b"PS-X EXE"
    struct.pack_into("<I", hdr, 0x10, entry)        # pc0
    struct.pack_into("<I", hdr, 0x18, load_addr)    # t_addr
    struct.pack_into("<I", hdr, 0x1C, len(data))    # t_size
    struct.pack_into("<I", hdr, 0x30, stack)        # s_addr
    hdr[0x4C:0x4C + 41] = b"Generado por tools/acebin.py desde ACE.BIN"[:41]
    pad = (-len(data)) % 2048
    return bytes(hdr) + data + bytes(pad)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    disc = Disc(argv[1])
    ab = AceBin(disc.read_file("ACE.BIN"))
    data, rest = ab.decompress()
    got = ab.word_sum(data)
    print(f"ACE.BIN: {ab.file_size} bytes -> {len(data)} bytes (carga {ab.load_addr:#x}, entrada {ab.entry:#x})")
    print(f"entrada sin consumir: {rest} bytes (relleno)")
    print(f"suma u32: calculada {got:#010x}, cabecera {ab.checksum:#010x} -> {'OK' if got == ab.checksum else 'FALLO'}")
    if got != ab.checksum:
        return 1
    out = psexe_wrap(data, ab.load_addr, ab.entry, ab.stack) if "--psexe" in argv else data
    with open(argv[2], "wb") as fh:
        fh.write(out)
    print(f"escrito {argv[2]} ({len(out)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
