"""acebin — descompresor de ACE.BIN, el modulo principal del juego.

ACE.BIN no es un PS-EXE: lleva una cabecera propietaria de Namco (magic
"PS-Y") y el codigo comprimido con un LZ propio. Lo carga SCES_020.66
(funcion en 0x801642D4 de la version EU), que hace exactamente esto:

  1. comprobar el magic "PS-Y"
  2. sumar las (tamano+3)/4 palabras u32 del archivo: debe dar 0
  3. descomprimir desde el offset (campo 0x14 & ~3) a la direccion de carga
  4. exigir que el final sea carga + tamano descomprimido
  5. sumar las palabras u32 de la salida: debe ser el campo 0x58
  6. Exec() con la estructura EXEC de PsyQ que empieza en 0x1C

CABECERA (0x5C bytes; verificada en EU y US):

    0x00  char[4]  magic "PS-Y"
    0x04  u32      ajuste para que la suma u32 de todo el archivo sea 0
    0x08  u32      direccion final = carga + tamano descomprimido
    0x0C  u32      tamano util del archivo (EU: exacto; US: 2 bytes menos)
    0x10  u32      tamano descomprimido
    0x14  u32      0x5C = offset del flujo comprimido
    0x1C  EXEC     estructura EXEC de PsyQ: pc0 (0x1C), gp0, t_addr (0x24),
                   t_size (0x28), d_addr, d_size, b_addr, b_size,
                   s_addr (0x3C), s_size
    0x58  u32      suma de las palabras u32 de la salida (mod 2^32)

FLUJO COMPRIMIDO, desde el offset del campo 0x14:

    u8 nbits                       # 12: bits de distancia (ventana 4096)
    repetir:
        u8 flags                   # 0 = fin del flujo
        consumir bits del 0 hacia arriba MIENTRAS quede mas de un bit: el bit
        mas alto es un centinela (con flags >= 0x80 salen 7 elementos)
            bit 1: copiar 1 byte literal
            bit 0: v = b0 << 8 | b1   (u16 big endian)
                   distancia = v & ((1 << nbits) - 1), y 0 significa 1 << nbits
                   longitud  = v >> nbits,            y 0 significa 1 << (16 - nbits)
                   copiar `longitud` bytes desde `distancia` atras

Pruebas (EU y US):
  - suma u32 del archivo entero == 0 y suma de la salida == campo 0x58
  - la salida mide exactamente el tamano declarado y el flujo termina con su
    byte 0 justo al final del tamano util
  - el algoritmo se dedujo primero a mano (cadenas conocidas) y despues se
    confirmo contra el descompresor de SCES_020.66 decompilado con Ghidra
  - el punto de entrada empieza con el bucle de limpieza de BSS de PsyQ

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


def word_sum(data, nwords=None):
    if nwords is None:
        nwords = len(data) // 4
    data = data + bytes(max(0, 4 * nwords - len(data)))
    return sum(struct.unpack_from(f"<{nwords}I", data, 0)) & 0xFFFFFFFF


class AceBin:
    def __init__(self, raw):
        if raw[:4] != MAGIC:
            raise ValueError("no es ACE.BIN (magic distinto)")
        (self.end_addr, self.file_size, self.size, self.data_off) = struct.unpack_from("<4I", raw, 0x08)
        self.entry = struct.unpack_from("<I", raw, 0x1C)[0]
        self.load_addr = struct.unpack_from("<I", raw, 0x24)[0]
        self.stack = struct.unpack_from("<I", raw, 0x3C)[0]
        self.checksum = struct.unpack_from("<I", raw, 0x58)[0]
        if self.file_size > len(raw):
            raise ValueError("el tamano declarado excede el archivo")
        if self.load_addr + self.size != self.end_addr:
            raise ValueError("carga + tamano != direccion final")
        self.raw = raw

    def file_sum(self):
        """Suma u32 de las (tamano+3)/4 palabras del archivo; el cargador exige 0."""
        return word_sum(self.raw[:self.file_size], (self.file_size + 3) >> 2)

    def decompress(self):
        """Devuelve (datos, offset_de_entrada_tras_el_terminador)."""
        b = self.raw
        i = self.data_off & ~3
        nbits = b[i]
        i += 1
        window = 1 << nbits
        max_len = 1 << (16 - nbits)
        out = bytearray()
        while True:
            flags = b[i]
            i += 1
            if flags == 0:
                break
            while flags > 1:
                bit = flags & 1
                flags >>= 1
                if bit:
                    out.append(b[i])
                    i += 1
                else:
                    v = b[i] << 8 | b[i + 1]
                    i += 2
                    dist = (v & (window - 1)) or window
                    length = (v >> nbits) or max_len
                    if dist > len(out):
                        raise ValueError(f"referencia antes del inicio en la salida {len(out)}")
                    for _ in range(length):
                        out.append(out[-dist])
        return bytes(out), i


def psexe_wrap(data, load_addr, entry, stack):
    """Envuelve el binario en una cabecera PS-X EXE estandar de 2048 bytes."""
    hdr = bytearray(2048)
    hdr[0:8] = b"PS-X EXE"
    struct.pack_into("<I", hdr, 0x10, entry)        # pc0
    struct.pack_into("<I", hdr, 0x18, load_addr)    # t_addr
    struct.pack_into("<I", hdr, 0x1C, len(data))    # t_size
    struct.pack_into("<I", hdr, 0x30, stack)        # s_addr
    note = b"Generado por tools/acebin.py desde ACE.BIN"
    hdr[0x4C:0x4C + len(note)] = note
    pad = (-len(data)) % 2048
    return bytes(hdr) + data + bytes(pad)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    ab = AceBin(Disc(argv[1]).read_file("ACE.BIN"))
    fsum = ab.file_sum()
    data, end = ab.decompress()
    osum = word_sum(data)
    print(f"ACE.BIN: {ab.file_size} bytes -> {len(data)} bytes (carga {ab.load_addr:#x}, entrada {ab.entry:#x})")
    checks = [
        ("suma u32 del archivo == 0", fsum == 0),
        (f"tamano de salida == {ab.size}", len(data) == ab.size),
        (f"terminador al final del tamano util ({end} <= {ab.file_size})", end <= ab.file_size and ab.file_size - end < 4),
        (f"suma u32 de la salida == {ab.checksum:#010x}", osum == ab.checksum),
    ]
    for name, ok in checks:
        print(f"  {'OK   ' if ok else 'FALLO'} {name}")
    if not all(ok for _, ok in checks):
        return 1
    out = psexe_wrap(data, ab.load_addr, ab.entry, ab.stack) if "--psexe" in argv else data
    with open(argv[2], "wb") as fh:
        fh.write(out)
    print(f"escrito {argv[2]} ({len(out)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
