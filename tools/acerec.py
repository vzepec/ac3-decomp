"""acerec — registros de ACE.BPB: tabla de secciones y volcado descomprimido.

Trabaja sobre las entradas ya extraidas con `make extract-bpb` (data/bpb/).

CABECERA COMUN DE REGISTRO (ver docs/04-formatos.md):

    u32 type;                  // tipo de recurso
    u32 header_size;           // = 8 + 4*n_secciones
    u32 section_offset[n];     // relativos al inicio del registro

La seccion k ocupa [offset[k], offset[k+1]) y la ultima llega al final del
registro (el final incluye el relleno hasta sector de 2048 del pack).

Cada seccion puede estar comprimida en ULZ (magic "Ulz\\x1a", ver
tools/ulz.py) o venir en claro.

Uso:
    python3 tools/acerec.py sections <registro.bin>        # tabla de secciones
    python3 tools/acerec.py dump <registro.bin> <dir>      # secciones descomprimidas
    python3 tools/acerec.py summary <dir_bpb>              # resumen por tipo/seccion
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ulz  # noqa: E402


class Record:
    def __init__(self, raw, name=""):
        self.raw = raw
        self.name = name
        self.type, self.header_size = struct.unpack_from("<II", raw, 0)
        n = (self.header_size - 8) // 4
        self.offsets = list(struct.unpack_from(f"<{n}I", raw, 8)) if n else []

    @classmethod
    def from_file(cls, path):
        """Devuelve un Record, o None si el archivo no sigue la convencion
        (las tablas de sistema 1..5 del BPB)."""
        raw = open(path, "rb").read()
        if len(raw) < 8:
            return None
        hsize = struct.unpack_from("<I", raw, 4)[0]
        if hsize < 8 or hsize > 2048 or hsize % 4:
            return None
        rec = cls(raw, os.path.basename(path))
        offs = rec.offsets
        if any(o >= len(raw) for o in offs) or any(offs[k] > offs[k + 1] for k in range(len(offs) - 1)):
            return None
        return rec

    def bounds(self):
        ends = self.offsets[1:] + [len(self.raw)]
        return list(zip(self.offsets, ends))

    def is_ulz(self, k):
        a, _ = self.bounds()[k]
        return self.raw[a:a + 4] == ulz.MAGIC

    def section(self, k):
        """Bytes de la seccion k, descomprimidos si son ULZ."""
        a, b = self.bounds()[k]
        if self.raw[a:a + 4] == ulz.MAGIC:
            return ulz.decompress(self.raw, a, b)
        return self.raw[a:b]


def cmd_sections(path):
    rec = Record.from_file(path)
    if rec is None:
        print("no es un registro con tabla de secciones")
        return 1
    print(f"{rec.name}: tipo {rec.type}, {len(rec.offsets)} secciones, {len(rec.raw)} bytes")
    print(f"{'k':>3} {'offset':>9} {'en disco':>9} {'ulz':>4} {'real':>9}  primeros bytes")
    for k, (a, b) in enumerate(rec.bounds()):
        data = rec.section(k)
        z = "si" if rec.is_ulz(k) else "-"
        print(f"{k:>3} {a:>#9x} {b - a:>9} {z:>4} {len(data):>9}  {data[:24].hex(' ')}")
    return 0


def cmd_dump(path, outdir):
    rec = Record.from_file(path)
    if rec is None:
        print("no es un registro con tabla de secciones")
        return 1
    os.makedirs(outdir, exist_ok=True)
    stem = os.path.splitext(rec.name)[0]
    for k in range(len(rec.offsets)):
        data = rec.section(k)
        out = os.path.join(outdir, f"{stem}_s{k:02d}.bin")
        with open(out, "wb") as fh:
            fh.write(data)
    print(f"{rec.name}: {len(rec.offsets)} secciones -> {outdir}")
    return 0


def cmd_summary(directory):
    """Por cada (tipo, seccion): cuantas son ULZ y rango de tamano real."""
    import collections
    agg = collections.defaultdict(lambda: [0, 0, None, 0])
    for name in sorted(os.listdir(directory)):
        rec = Record.from_file(os.path.join(directory, name))
        if rec is None:
            continue
        for k in range(len(rec.offsets)):
            n = len(rec.section(k))
            st = agg[(rec.type, k)]
            st[0] += 1
            st[1] += int(rec.is_ulz(k))
            st[2] = n if st[2] is None else min(st[2], n)
            st[3] = max(st[3], n)
    print(f"{'tipo':>5} {'sec':>4} {'num':>4} {'ulz':>4} {'min':>8} {'max':>8}")
    for (t, k), (n, z, lo, hi) in sorted(agg.items()):
        print(f"{t:>5} {k:>4} {n:>4} {z:>4} {lo:>8} {hi:>8}")
    return 0


def main(argv):
    if len(argv) >= 3 and argv[1] == "sections":
        return cmd_sections(argv[2])
    if len(argv) >= 4 and argv[1] == "dump":
        return cmd_dump(argv[2], argv[3])
    if len(argv) >= 3 and argv[1] == "summary":
        return cmd_summary(argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
