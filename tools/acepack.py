"""acepack — indices y contenedores de Ace Combat 3 (ACE.SPH/SPB, ACE.BPH/BPB).

FORMATO DEL INDICE (verificado contra las 4 combinaciones EU/US x SPH/BPH):

    offset  tipo      campo
    0x00    char[4]   magic  = "AC3E"
    0x04    u32       kind   = 1 para SPH (pack de streaming, ACE.SPB)
                             = 3 para BPH (pack de bancos,    ACE.BPB)
    0x08    u32       date   BCD. SPH: 0x19991019 (EU) / 0x19991130 (US)
                             BPH: solo 0x1019 / 0x1130 (sin el ano)
    0x0C    u32       unk    SPH: 0 ; BPH: 0x16C3 (EU) / 0x16CF (US). SIN RESOLVER
    0x10    u32       count  numero de entradas
    0x14    Entry[count]

    struct Entry {      // 8 bytes
        u32 tag;        // byte alto = categoria
                        // BPB: 24 bits bajos = tamano del registro en u32 (verificado)
                        // SPB: SIN RESOLVER
        u32 sector;     // offset en sectores de 2048 bytes dentro del pack
    };

Comprobacion: 0x14 + 8*count == tamano del archivo de indice, en los 4 casos.

La extension de cada entrada se deduce de la siguiente: la entrada i ocupa
[sector_i, sector_{i+1}), y la ultima llega al final del pack. Los 24 bits
bajos de `tag` en el BPB son el tamano en palabras de 32 bits (127/127 en EU
y US, comando `tags`); en el SPB no (SIN RESOLVER).

Cada registro del pack empieza con un u32 que actua de tipo de recurso
(se han visto 4, 5, 6, 7, 8, 0x11...). Dentro aparecen punteros a RAM de
PS1 (0x800Cxxxx) y cadenas de texto ("BGOK02", "Last Flight").

Uso:
    python3 tools/acepack.py <imagen.bin> index          # volcar los indices
    python3 tools/acepack.py <imagen.bin> inventory      # inventario + cabeceras
    python3 tools/acepack.py <imagen.bin> records        # cabecera comun de cada registro
    python3 tools/acepack.py <imagen.bin> tags           # semantica del tag
    python3 tools/acepack.py <imagen.bin> extract BPB <dir>   # extraer entradas
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psxiso import Disc  # noqa: E402

SECTOR = 2048
HDR = 0x14
PAIRS = {"SPB": ("ACE.SPH", "ACE.SPB"), "BPB": ("ACE.BPH", "ACE.BPB")}


class Index:
    def __init__(self, raw):
        if raw[:4] != b"AC3E":
            raise ValueError(f"magic inesperado: {raw[:4]!r}")
        self.magic, self.kind, self.date, self.unk, self.count = struct.unpack("<4sIIII", raw[:HDR])
        if HDR + 8 * self.count != len(raw):
            raise ValueError(
                f"tamano incoherente: 0x14+8*{self.count}={HDR + 8 * self.count} != {len(raw)}"
            )
        self.entries = [
            struct.unpack("<II", raw[HDR + i * 8: HDR + i * 8 + 8]) for i in range(self.count)
        ]

    def extents(self, pack_size):
        """Devuelve [(i, tag, sector, nbytes)] en orden de indice.

        IMPORTANTE: las entradas NO vienen ordenadas por sector (comprobado en
        ACE.SPH: la entrada 71 apunta al sector 19400, antes que la 70). El
        tamano se deduce ordenando por sector y tomando el siguiente limite.
        """
        order = sorted(range(self.count), key=lambda i: self.entries[i][1])
        bound = {}
        for pos, i in enumerate(order):
            sec = self.entries[i][1]
            nxt = self.entries[order[pos + 1]][1] * SECTOR if pos + 1 < len(order) else pack_size
            bound[i] = max(0, nxt - sec * SECTOR)
        return [(i, self.entries[i][0], self.entries[i][1], bound[i]) for i in range(self.count)]


def load(disc, which):
    idx_name, pack_name = PAIRS[which]
    idx = Index(disc.read_file(idx_name))
    pack = disc.find(pack_name)
    return idx, pack


def cmd_index(disc):
    for which in PAIRS:
        idx, pack = load(disc, which)
        print(f"\n=== {PAIRS[which][0]} -> {PAIRS[which][1]} ===")
        print(f"kind={idx.kind} date={idx.date:08x} unk=0x{idx.unk:x} count={idx.count}")
        print(f"pack: lba={pack.lba} size={pack.size} ({pack.size // SECTOR} sectores)")
        cats = {}
        for _, tag, _, _ in idx.extents(pack.size):
            cats.setdefault(tag >> 24, 0)
            cats[tag >> 24] += 1
        print("categorias (byte alto del tag):", {f"0x{k:02X}": v for k, v in sorted(cats.items())})


def cmd_inventory(disc):
    for which in PAIRS:
        idx, pack = load(disc, which)
        print(f"\n=== inventario {which} ({idx.count} entradas) ===")
        print(f"{'#':>4} {'tag':>10} {'sector':>8} {'bytes':>10} {'tipo':>5}  cadenas / primeros bytes")
        for i, tag, sec, nbytes in idx.extents(pack.size):
            if sec * SECTOR >= pack.size:
                print(f"{i:>4} 0x{tag:08x} {sec:>8} {'FUERA':>10}")
                continue
            head = disc.read(pack.lba + sec, min(nbytes, 512) or 512)
            rtype = struct.unpack("<I", head[:4])[0] if len(head) >= 4 else -1
            strs = [s.decode("latin1") for s in re.findall(rb"[\x20-\x7e]{4,}", head[:512])]
            strs = [s for s in strs if re.search(r"[A-Za-z]{3}", s)][:3]
            note = " | ".join(strs) if strs else head[4:20].hex(" ")
            print(f"{i:>4} 0x{tag:08x} {sec:>8} {nbytes:>10} {rtype:>5}  {note}")


def cmd_records(disc, which):
    """Analiza la cabecera comun de cada registro del pack.

    Layout verificado en 122 de los 127 registros de ACE.BPB:

        u32 type;                  // tipo de recurso
        u32 header_size;           // = 8 + 4*n_secciones
        u32 section_offset[n];     // offsets relativos al inicio del registro,
                                   // monotonos crecientes y dentro del registro

    Las 5 primeras entradas del BPB (indices 1..5) NO siguen esta convencion:
    son tablas de sistema (memory card, nombres de BGM, textos).
    """
    import collections
    idx, pack = load(disc, which)
    stats = collections.defaultdict(lambda: {"n": 0, "hsize": set(), "nsec": set(), "ok": 0})
    odd = []
    for i, tag, sec, nbytes in idx.extents(pack.size):
        if nbytes < 64:
            continue
        head = disc.read(pack.lba + sec, min(2048, nbytes))
        rtype, hsize = struct.unpack("<II", head[:8])
        if hsize < 8 or hsize > 2048 or hsize % 4:
            odd.append((i, rtype, hsize))
            continue
        nsec = (hsize - 8) // 4
        offs = struct.unpack(f"<{nsec}I", head[8:8 + nsec * 4]) if nsec else ()
        good = all(offs[k] <= offs[k + 1] for k in range(len(offs) - 1)) and all(o < nbytes for o in offs)
        st = stats[rtype]
        st["n"] += 1
        st["hsize"].add(hsize)
        st["nsec"].add(nsec)
        st["ok"] += int(good)
    print(f"=== registros de {PAIRS[which][1]} ===")
    print(f"{'tipo':>5} {'num':>5} {'header':>8} {'secciones':>10}  tabla coherente")
    for t in sorted(stats):
        st = stats[t]
        hs = ",".join(hex(h) for h in sorted(st["hsize"]))
        ns = ",".join(str(x) for x in sorted(st["nsec"]))
        print(f"{t:>5} {st['n']:>5} {hs:>8} {ns:>10}  {st['ok']}/{st['n']}")
    if odd:
        if not stats:
            print(f"  ninguno: las {len(odd)} entradas son datos crudos de streaming,")
            print("  no registros con cabecera (esperado en ACE.SPB)")
            return
        print("\nregistros que NO siguen la convencion (tablas de sistema):")
        for i, rtype, hsize in odd[:12]:
            print(f"  entrada {i}: u32[0]={rtype} u32[1]=0x{hsize:08x}")
        if len(odd) > 12:
            print(f"  ... y {len(odd) - 12} mas")


def cmd_tags(disc):
    """Comprueba la semantica del tag en ACE.BPB.

    Verificado (EU y US, 127/127): los 24 bits bajos del tag son el tamano
    exacto del registro en palabras de 32 bits. La extension en el pack es ese
    tamano redondeado a sector (2048). En ACE.SPB la regla NO se cumple: alli
    el tag significa otra cosa (SIN RESOLVER).
    """
    for which in PAIRS:
        idx, pack = load(disc, which)
        ok = 0
        for i, tag, sec, nbytes in idx.extents(pack.size):
            size = (tag & 0xFFFFFF) * 4
            ok += int(size <= nbytes and nbytes - size < SECTOR)
        print(f"{PAIRS[which][1]}: tamano = (tag & 0xFFFFFF) * 4 cumple en {ok}/{idx.count}")


def cmd_extract(disc, which, outdir):
    idx, pack = load(disc, which)
    os.makedirs(outdir, exist_ok=True)
    for i, tag, sec, nbytes in idx.extents(pack.size):
        if sec * SECTOR >= pack.size or nbytes == 0:
            continue
        data = disc.read(pack.lba + sec, nbytes)
        rtype = struct.unpack("<I", data[:4])[0]
        name = f"{i:04d}_tag{tag:08x}_t{rtype:02d}.bin"
        with open(os.path.join(outdir, name), "wb") as fh:
            fh.write(data)
        print(f"{name}  {nbytes} bytes")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    disc = Disc(argv[1])
    cmd = argv[2]
    if cmd == "index":
        cmd_index(disc)
    elif cmd == "inventory":
        cmd_inventory(disc)
    elif cmd == "records":
        for which in PAIRS:
            cmd_records(disc, which)
            print()
    elif cmd == "tags":
        cmd_tags(disc)
    elif cmd == "extract":
        if len(argv) < 5 or argv[3] not in PAIRS:
            print("uso: extract <SPB|BPB> <dir>")
            return 2
        cmd_extract(disc, argv[3], argv[4])
    else:
        print("comando desconocido:", cmd)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
