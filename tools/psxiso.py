"""psxiso — lectura de imagenes de disco PlayStation (ISO9660).

Soporta pistas crudas MODE1/2352 y MODE2/2352 (deteccion automatica del
tamano de sector y del modo leyendo la cabecera de sincronismo), y tambien
imagenes ya desnudas de 2048 bytes por sector.

Uso como libreria:
    from psxiso import Disc
    d = Disc(path)
    for e in d.list():  print(e.name, e.size, e.lba)
    data = d.read_file("ACE.SPH")

Uso como CLI:
    python3 psxiso.py <imagen.bin> list
    python3 psxiso.py <imagen.bin> extract <dir_salida>
"""
import os
import struct
import sys

USER = 2048
SYNC = b"\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00"


class Entry:
    __slots__ = ("name", "size", "lba", "is_dir")

    def __init__(self, name, size, lba, is_dir):
        self.name = name
        self.size = size
        self.lba = lba
        self.is_dir = is_dir

    def __repr__(self):
        kind = "D" if self.is_dir else "F"
        return f"<{kind} {self.name} size={self.size} lba={self.lba}>"


class Disc:
    """Acceso por sector logico a una imagen de disco PSX."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.sector_size, self.data_offset = self._detect()

    def _detect(self):
        head = self.f.read(16)
        if head[:12] == SYNC:
            mode = head[15]
            # MODE1 -> datos de usuario en 16; MODE2 form1 -> en 24
            return 2352, (16 if mode == 1 else 24)
        return USER, 0

    def sector(self, lba):
        self.f.seek(lba * self.sector_size + self.data_offset)
        return self.f.read(USER)

    def read(self, lba, nbytes):
        out = bytearray()
        n = (nbytes + USER - 1) // USER
        for i in range(n):
            out += self.sector(lba + i)
        return bytes(out[:nbytes])

    # ---- ISO9660 ----

    def _root(self):
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001":
            raise ValueError(f"{self.path}: no se encontro un PVD ISO9660 en el sector 16")
        rec = pvd[156:156 + 34]
        lba = struct.unpack("<I", rec[2:6])[0]
        size = struct.unpack("<I", rec[10:14])[0]
        return lba, size

    def volume_id(self):
        pvd = self.sector(16)
        return pvd[40:72].decode("latin1").strip()

    def publisher(self):
        pvd = self.sector(16)
        return pvd[318:446].decode("latin1").strip()

    def list(self):
        lba, size = self._root()
        out = []
        self._walk(lba, size, "/", out, 0)
        return out

    def _walk(self, lba, length, path, out, depth):
        data = self.read(lba, length)
        i = 0
        while i < len(data):
            rlen = data[i]
            if rlen == 0:
                # relleno hasta el final del sector
                i = (i // USER + 1) * USER
                if i >= len(data):
                    break
                continue
            rec = data[i:i + rlen]
            ext = struct.unpack("<I", rec[2:6])[0]
            size = struct.unpack("<I", rec[10:14])[0]
            flags = rec[25]
            nlen = rec[32]
            name = rec[33:33 + nlen].decode("latin1")
            i += rlen
            if nlen == 1 and name in ("\x00", "\x01"):
                continue
            name = name.split(";")[0]
            is_dir = bool(flags & 2)
            out.append(Entry(path + name, size, ext, is_dir))
            if is_dir and depth < 8:
                self._walk(ext, size, path + name + "/", out, depth + 1)

    def find(self, name):
        want = name.lstrip("/").upper()
        for e in self.list():
            if e.name.lstrip("/").upper() == want:
                return e
        return None

    def read_file(self, name):
        e = self.find(name)
        if e is None:
            raise FileNotFoundError(name)
        return self.read(e.lba, e.size)

    def extract_all(self, outdir):
        os.makedirs(outdir, exist_ok=True)
        written = []
        for e in self.list():
            if e.is_dir:
                continue
            dest = os.path.join(outdir, e.name.lstrip("/").replace("/", os.sep))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as fh:
                fh.write(self.read(e.lba, e.size))
            written.append(dest)
        return written


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    disc = Disc(argv[1])
    cmd = argv[2]
    if cmd == "list":
        print(f"# {argv[1]}")
        print(f"# sector={disc.sector_size} offset_datos={disc.data_offset}")
        print(f"# volumen={disc.volume_id()!r} editor={disc.publisher()!r}")
        total = 0
        for e in disc.list():
            print(f"{e.size:>12}  lba={e.lba:<8} {'D' if e.is_dir else 'F'}  {e.name}")
            if not e.is_dir:
                total += e.size
        print(f"# total archivos: {total} bytes ({total / 1048576:.1f} MiB)")
    elif cmd == "extract":
        if len(argv) < 4:
            print("falta el directorio de salida")
            return 2
        for p in disc.extract_all(argv[3]):
            print("escrito:", p)
    else:
        print("comando desconocido:", cmd)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
