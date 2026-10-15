"""psexe — cabeceras de ejecutables PlayStation del disco de Ace Combat 3.

Cubre dos formatos:

1. PS-EXE estandar de Sony (magic "PS-X EXE"), usado por el binario de
   arranque (SCES_020.66 / SLUS_009.72) y por ACE.EXE:

       0x00 char[8] magic = "PS-X EXE"
       0x10 u32 pc0        punto de entrada
       0x14 u32 gp0
       0x18 u32 t_addr     direccion de carga
       0x1C u32 t_size     tamano del segmento de texto
       0x30 u32 s_addr     pila
       0x4C char[]  cadena de licencia / region

2. Cabecera propietaria de Namco usada por ACE.BIN (magic "PS-Y" + 4 bytes
   variables). Campos deducidos y verificados por coherencia cruzada:

       0x00 char[8] magic        "PS-Y4a\\xe3w" en la version europea
       0x08 u32 end_addr         = load_addr + unpacked_size  (VERIFICADO)
       0x0C u32 packed_size?     0x6504C = 414796, cerca del tamano del
                                 archivo (413772). SIN CONFIRMAR
       0x10 u32 unpacked_size    = 0xAB000 = 703488           (VERIFICADO)
       0x14 u32 ?                0x5C                          SIN RESOLVER
       0x1C u32 pc0              punto de entrada             (VERIFICADO)
       0x24 u32 load_addr        0x80010000                   (VERIFICADO)
       0x28 u32 size             = unpacked_size              (VERIFICADO)
       0x3C u32 s_addr           0x801FFFF0                   (VERIFICADO)

   El cuerpo de ACE.BIN esta COMPRIMIDO o cifrado (entropia ~7.4 bits/byte
   y ausencia de prologos MIPS en claro). El descompresor vive en el binario
   de arranque. Ver docs/02-binarios.md.

Uso:
    python3 tools/psexe.py <imagen.bin>
"""
import collections
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psxiso import Disc  # noqa: E402

CANDIDATES = ("SCES_020.66", "SLUS_009.72", "SLPS_022.01", "ACE.EXE", "ACE.BIN")


def entropy(b):
    if not b:
        return 0.0
    c = collections.Counter(b)
    n = len(b)
    return -sum((v / n) * math.log2(v / n) for v in c.values())


def mips_prologues(b):
    """Cuenta prologos 'addiu sp, sp, -N'.

    La instruccion es la palabra 0x27BDFFxx, y MIPS en PS1 es LITTLE ENDIAN,
    asi que en el archivo aparece como los bytes xx FF BD 27. Buscar
    "27 bd ff" seria el orden equivocado y daria siempre cero.
    """
    return len(re.findall(rb"\xff\xbd\x27", b))


def report(name, raw):
    print(f"\n===== {name}  ({len(raw)} bytes) =====")
    magic = raw[:8]
    body = raw[2048:]
    print(f"  magic         : {magic!r}")
    if magic == b"PS-X EXE":
        pc, gp, taddr, tsize = struct.unpack("<IIII", raw[0x10:0x20])
        saddr = struct.unpack("<I", raw[0x30:0x34])[0]
        lic = raw[0x4C:0x80].decode("latin1").strip("\x00").strip()
        print("  formato       : PS-EXE estandar de Sony")
        print(f"  entrada pc0   : 0x{pc:08X}")
        print(f"  gp0           : 0x{gp:08X}")
        print(f"  carga t_addr  : 0x{taddr:08X}")
        print(f"  t_size        : {tsize} (0x{tsize:X})  -> fin 0x{taddr + tsize:08X}")
        print(f"  pila s_addr   : 0x{saddr:08X}")
        print(f"  licencia      : {lic}")
        print(f"  coherencia    : 2048+t_size = {2048 + tsize} vs archivo {len(raw)} "
              f"{'OK (sin comprimir)' if 2048 + tsize == len(raw) else 'DISCREPA'}")
    elif magic[:4] == b"PS-Y":
        end, packed, unpacked, unk14 = struct.unpack("<IIII", raw[0x08:0x18])
        pc = struct.unpack("<I", raw[0x1C:0x20])[0]
        load, size = struct.unpack("<II", raw[0x24:0x2C])
        saddr = struct.unpack("<I", raw[0x3C:0x40])[0]
        print("  formato       : cabecera propietaria de Namco (variante PS-Y)")
        print(f"  end_addr      : 0x{end:08X}")
        print(f"  packed_size?  : {packed} (0x{packed:X})   archivo={len(raw)}")
        print(f"  unpacked_size : {unpacked} (0x{unpacked:X})")
        print(f"  campo 0x14    : 0x{unk14:X}   SIN RESOLVER")
        print(f"  entrada pc0   : 0x{pc:08X}")
        print(f"  carga         : 0x{load:08X}  size=0x{size:X}")
        print(f"  pila s_addr   : 0x{saddr:08X}")
        ok = (load + unpacked == end) and (size == unpacked)
        print(f"  coherencia    : load+unpacked==end y size==unpacked -> {'OK' if ok else 'DISCREPA'}")
    else:
        print("  formato       : desconocido")
        print(f"  primeros bytes: {raw[:32].hex(' ')}")

    print(f"  entropia cuerpo: {entropy(body):.2f} bits/byte")
    pro = mips_prologues(body)
    dens = pro / max(1, len(body) / 1024)
    print(f"  prologos MIPS  : {pro} ({dens:.2f} por KiB)  "
          f"({'codigo MIPS en claro' if dens > 0.2 else 'sin codigo en claro -> empaquetado'})")
    ids = [s.decode("latin1") for s in re.findall(rb"\$Id: [^$]{5,90}\$", body)]
    if ids:
        print("  RCS Ids de libreria (PsyQ de Sony):")
        for s in ids:
            print(f"    {s}")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    disc = Disc(argv[1])
    print(f"disco: {argv[1]}")
    print(f"volumen={disc.volume_id()!r} editor={disc.publisher()!r}")
    found = 0
    for name in CANDIDATES:
        e = disc.find(name)
        if e is None:
            continue
        found += 1
        report(name, disc.read(e.lba, e.size))
    if not found:
        print("no se encontro ningun ejecutable conocido en este disco")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
