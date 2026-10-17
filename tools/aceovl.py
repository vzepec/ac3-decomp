"""aceovl — modos del juego y overlays de codigo de ACE.BIN.

ACE.BIN tiene una tabla de modos (en la version EU, en 0x800AE544) que usa el
despachador FUN_80025EB0. Cada modo ocupa 16 bytes:

    u32 pasos;      // tabla de punteros a funciones (un paso cada una)
    u32 id;         // 0xFFFFFFFF = codigo residente; k = overlay k
    u32 inicio;     // direccion de carga
    u32 fin;        // direccion final

Los overlays se cargan en 0x800C9DC0 (justo despues de la BSS de ACE.BIN) y
el overlay k es la ENTRADA k de ACE.BPB. Pruebas:

  - fin - inicio == tamano del registro segun su tag ((tag & 0xFFFFFF) * 4)
    en 4 de los 5 overlays; en el 4 difieren 4 bytes
  - los primeros pasos del modo apuntan a inicios de funcion: justo detras de
    un `jr ra` de la funcion anterior
  - el codigo de los overlays referencia variables globales del residente
    (p. ej. 0x800BACD0)

Las entradas 1-5 son por tanto codigo, no "tablas de sistema": la etapa 1 las
habia clasificado mal.

La tabla de modos esta en una direccion distinta en cada version; aqui se
usa la EU por defecto (--table para otra).

Uso:
    python3 tools/aceovl.py list <ace.bin plano> <dir_bpb>
    python3 tools/aceovl.py extract <ace.bin plano> <dir_bpb> <dir_salida>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ACE_BASE = 0x80010000
MODE_TABLE_EU = 0x800AE544
RESIDENT = 0xFFFFFFFF


def read_modes(ace, table=MODE_TABLE_EU):
    """[(modo, pasos, id, inicio, fin)] hasta el primer registro incoherente."""
    out = []
    for m in range(256):
        off = table - ACE_BASE + 16 * m
        if off + 16 > len(ace):
            break
        steps, oid, lo, hi = struct.unpack_from("<4I", ace, off)
        if not (0x80000000 <= steps < 0x80200000 and 0x80000000 <= lo < hi < 0x80200000):
            break
        out.append((m, steps, oid, lo, hi))
    return out


def bpb_entry(dir_bpb, k):
    for name in sorted(os.listdir(dir_bpb)):
        if name.startswith(f"{k:04d}_tag"):
            tag = int(name.split("_tag")[1][:8], 16)
            return open(os.path.join(dir_bpb, name), "rb").read(), (tag & 0xFFFFFF) * 4
    return None, None


def step_checks(image, base, steps, nsteps=8):
    """Cuantos de los primeros pasos caen tras un `jr ra` dentro de la imagen."""
    ok = total = 0
    for i in range(nsteps):
        so = steps - base + 4 * i
        if not 0 <= so <= len(image) - 4:
            break
        target = struct.unpack_from("<I", image, so)[0]
        to = target - base
        if not 8 <= to < len(image):
            break
        total += 1
        prev = struct.unpack_from("<2I", image, to - 8)
        ok += int(0x03E00008 in prev)
    return ok, total


def psexe(data, load, entry):
    hdr = bytearray(2048)
    hdr[0:8] = b"PS-X EXE"
    struct.pack_into("<I", hdr, 0x10, entry)
    struct.pack_into("<I", hdr, 0x18, load)
    struct.pack_into("<I", hdr, 0x1C, len(data))
    note = b"Overlay de ACE.BIN extraido por tools/aceovl.py"
    hdr[0x4C:0x4C + len(note)] = note
    return bytes(hdr) + data + bytes((-len(data)) % 2048)


def main(argv):
    if len(argv) < 4 or argv[1] not in ("list", "extract"):
        print(__doc__)
        return 2
    table = int(argv[argv.index("--table") + 1], 0) if "--table" in argv else MODE_TABLE_EU
    ace = open(argv[2], "rb").read()
    modes = read_modes(ace, table)
    if argv[1] == "extract":
        os.makedirs(argv[4], exist_ok=True)
    bad = 0
    for m, steps, oid, lo, hi in modes:
        if oid == RESIDENT:
            ok, total = step_checks(ace, ACE_BASE, steps)
            print(f"modo {m:2d}: residente  pasos {steps:#x}  pasos tras jr ra {ok}/{total}")
            continue
        data, tsize = bpb_entry(argv[3], oid)
        if data is None:
            print(f"modo {m:2d}: overlay {oid}: no se encuentra la entrada {oid} en {argv[3]}")
            bad += 1
            continue
        image = data[:tsize]
        ok, total = step_checks(image, lo, steps)
        same = "==" if hi - lo == tsize else f"!= (dif {hi - lo - tsize})"
        print(f"modo {m:2d}: overlay {oid} = entrada {oid} del BPB  rango {lo:#x}-{hi:#x} ({hi - lo} B) "
              f"{same} tag {tsize} B  pasos tras jr ra {ok}/{total}")
        if argv[1] == "extract":
            entry = struct.unpack_from("<I", image, steps - lo)[0] if 0 <= steps - lo < len(image) else lo
            out = os.path.join(argv[4], f"overlay{oid}.exe")
            with open(out, "wb") as fh:
                fh.write(psexe(image, lo, entry))
            print(f"    -> {out}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
