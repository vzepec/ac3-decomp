"""acemodel — modelos 3D de los registros de tipo 17 de ACE.BPB.

Trabaja sobre secciones ya descomprimidas (tools/acerec.py dump).

TABLA DE OFFSETS (contenedor generico, lo usan casi todas las secciones):

    u32 count;
    u32 offset[count];      // offset[0] == 4 + 4*count ; monotonos
    // la entrada i ocupa [offset[i], offset[i+1]) y la ultima hasta el final

CABECERA DE MODELO (verificada en 14.095 modelos de los 41 registros):

    u16 zero;               // siempre 0
    u16 extent;             // teselas: 4387 (constante); objetos: >= max|coord|
    u32 off_vertices;       // 0x18 en teselas (s01), 0x24 en objetos (s04)
    u32 off_unk;            // 0 en todos los vistos             SIN RESOLVER
    u32 off_prims;          // bloque de primitivas               SIN RESOLVER
    u32 off_end;            // tercer bloque / final              SIN RESOLVER
    [s04: u32 0 ; s16 bbox[6] = xmax,xmin, ymax,ymin, zmax,zmin]
    [s01: s16 ymin?, ymax?                                        SIN RESOLVER]

BLOQUE DE VERTICES, en off_vertices:

    u32 n;
    struct {                // grupos de 3 vertices, 20 bytes
        s16 x0, y0, x1, y1, x2, y2;
        s16 z0, z1, z2;
        s16 pad;
    } group[ceil(n/3)];

Es el layout que pide el GTE para RTPT (transformar 3 vertices de una vez):
cada (x,y) es una palabra VXY cargable con lwc2 y los z van aparte (VZ).

Pruebas de que la lectura es correcta:
  - off_vertices + 4 + 20*ceil(n/3) == off_prims en 14.095/14.095 modelos
  - en los 421 objetos de s04 la bbox de la cabecera es EXACTAMENTE el
    minimo/maximo de los vertices decodificados (421/421)
  - la tesela de mar (plana) da las cuatro esquinas (+-1024, -1126, +-1024)

Uso:
    python3 tools/acemodel.py list <seccion.bin>            # modelos de la seccion
    python3 tools/acemodel.py verts <seccion.bin> <i>       # vertices del modelo i
    python3 tools/acemodel.py obj <seccion.bin> <i> <out>   # nube de puntos .obj
    python3 tools/acemodel.py check <dir_bpb>               # validacion global
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def offset_table(buf):
    """Devuelve [(ini, fin)] de una tabla de offsets, o [] si no lo es."""
    if len(buf) < 8:
        return []
    n = struct.unpack_from("<I", buf, 0)[0]
    if n == 0 or 4 + 4 * n > len(buf):
        return []
    offs = list(struct.unpack_from(f"<{n}I", buf, 4))
    if offs[0] != 4 + 4 * n or any(offs[i] > offs[i + 1] for i in range(n - 1)) or offs[-1] > len(buf):
        return []
    ends = offs[1:] + [len(buf)]
    return list(zip(offs, ends))


class Model:
    def __init__(self, raw):
        if len(raw) < 24 or raw[:2] != b"\0\0":
            raise ValueError("no es un modelo")
        (self.extent, self.off_vertices, self.off_unk,
         self.off_prims, self.off_end) = struct.unpack_from("<HIIII", raw, 2)
        if self.off_vertices not in (0x18, 0x24) or self.off_prims > len(raw):
            raise ValueError("cabecera de modelo no reconocida")
        self.raw = raw
        self.bbox = struct.unpack_from("<6h", raw, 24) if self.off_vertices == 0x24 else None
        n = struct.unpack_from("<I", raw, self.off_vertices)[0]
        groups = (n + 2) // 3
        if self.off_vertices + 4 + 20 * groups != self.off_prims:
            raise ValueError("bloque de vertices incoherente con off_prims")
        verts = []
        base = self.off_vertices + 4
        for g in range(groups):
            x0, y0, x1, y1, x2, y2, z0, z1, z2, _ = struct.unpack_from("<10h", raw, base + 20 * g)
            verts += [(x0, y0, z0), (x1, y1, z1), (x2, y2, z2)]
        self.vertices = verts[:n]

    def real_bbox(self):
        xs, ys, zs = zip(*self.vertices)
        return (max(xs), min(xs), max(ys), min(ys), max(zs), min(zs))


def models(buf):
    """[(indice, Model)] de una seccion con tabla de offsets de modelos."""
    out = []
    for i, (a, b) in enumerate(offset_table(buf)):
        try:
            out.append((i, Model(buf[a:b])))
        except (ValueError, struct.error):
            pass
    return out


def write_obj(path, model):
    with open(path, "w") as fh:
        fh.write("# vertices de un modelo de Ace Combat 3 (sin caras: primitivas SIN RESOLVER)\n")
        fh.write("# eje Y de PS1 hacia abajo: se invierte para visores estandar\n")
        for x, y, z in model.vertices:
            fh.write(f"v {x} {-y} {z}\n")


def cmd_check(directory):
    from acerec import Record
    total = size_ok = bbox_n = bbox_ok = 0
    for name in sorted(os.listdir(directory)):
        rec = Record.from_file(os.path.join(directory, name))
        if rec is None or rec.type != 17:
            continue
        for k in range(len(rec.offsets)):
            buf = rec.section(k)
            for a, b in offset_table(buf):
                raw = buf[a:b]
                if len(raw) < 24 or raw[:2] != b"\0\0":
                    continue
                if struct.unpack_from("<I", raw, 4)[0] not in (0x18, 0x24):
                    continue
                total += 1
                try:
                    m = Model(raw)
                except (ValueError, struct.error):
                    continue
                size_ok += 1
                if m.bbox:
                    bbox_n += 1
                    bbox_ok += int(m.bbox == m.real_bbox())
    print(f"modelos candidatos: {total}")
    print(f"bloque de vertices coherente con off_prims: {size_ok}/{total}")
    print(f"bbox de cabecera == bbox de vertices (objetos): {bbox_ok}/{bbox_n}")
    return 0 if size_ok == total and bbox_ok == bbox_n else 1


def main(argv):
    if len(argv) >= 3 and argv[1] == "check":
        return cmd_check(argv[2])
    if len(argv) >= 3 and argv[1] == "list":
        buf = open(argv[2], "rb").read()
        for i, m in models(buf):
            extra = f" bbox={m.bbox}" if m.bbox else ""
            print(f"{i:4d} extent={m.extent:5d} vertices={len(m.vertices):4d}{extra}")
        return 0
    if len(argv) >= 4 and argv[1] in ("verts", "obj"):
        buf = open(argv[2], "rb").read()
        i = int(argv[3])
        a, b = offset_table(buf)[i]
        m = Model(buf[a:b])
        if argv[1] == "verts":
            print(f"modelo {i}: {len(m.vertices)} vertices, bbox real {m.real_bbox()}")
            for j, v in enumerate(m.vertices):
                print(f"  v{j:<4d} x={v[0]:6d} y={v[1]:6d} z={v[2]:6d}")
        else:
            write_obj(argv[4], m)
            print(f"{argv[4]}: {len(m.vertices)} vertices")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
