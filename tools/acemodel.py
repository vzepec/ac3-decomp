"""acemodel — modelos 3D de los registros de tipo 17 de ACE.BPB.

Trabaja sobre secciones ya descomprimidas (tools/acerec.py dump).

TABLA DE OFFSETS (contenedor generico, lo usan casi todas las secciones):

    u32 count;
    u32 offset[count];      // offset[0] == 4 + 4*count ; monotonos
    // la entrada i ocupa [offset[i], offset[i+1]) y la ultima hasta el final

Hay dos familias: Model (teselas s01 y objetos s04, abajo) y LodModel
(aviones y armas de s02/s03, ver la clase).

CABECERA DE MODELO (verificada en 14.095 modelos de los 41 registros):

    u16 zero;               // siempre 0
    u16 extent;             // teselas: 4387 (constante); objetos: >= max|coord|
    u32 off_vertices;       // 0x18 en teselas (s01), 0x24 en objetos (s04)
    u32 off_unk;            // 0 en todos los vistos             SIN RESOLVER
    u32 off_prims;          // bloque 1: primitivas planas (POLY_FT4)
    u32 off_end;            // bloque 2: primitivas Gouraud (POLY_GT4)
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

PRIMITIVAS (verificadas en 14.095/14.095 modelos):

    bloque 1 en off_prims: u32 n ; n x 36 bytes, POLY_FT4 (codigo GPU 0x2C)
    bloque 2 en off_end:   u32 n2; n2 x 48 bytes, POLY_GT4 (codigo GPU 0x3C)
    En las teselas (s01) cada bloque va seguido de 12 bytes de ceros por
    primitiva; en los objetos (s04) no hay relleno.

    off  tipo     campo
    0    u8       h0 (0..4)                                  SIN RESOLVER
    1    u8       h1: 0 = cuadrilatero plano, 1 = triangulo, 2 = cuadrilatero
                  no plano partido en (i0,i1,i2) + (i1,i3,i2)
    2    u16      0
    4    u16 x2   e0, e1: offsets en bytes a la NORMAL de la cara en la
                  seccion 5 del registro (SVECTOR, unitaria en 4.12, sentido
                  opuesto a (v1-v0)x(v2-v0)). e0 == e1 salvo con h1 = 2, en
                  que cada triangulo tiene la suya. Ver comando `normals`.
    8    u8[8]    por vertice: (offset xy, offset z) en bytes dentro del
                  bloque de vertices, listos para el GTE (redundantes con
                  los indices)
    16   u8[4]    indices de vertice i0..i3 (orden de tira: cara i0,i1,i3,i2;
                  triangulo si i3 == i2)
    20   u8,u8    u0,v0      22 u16 CLUT (x=(c&63)*16, y=c>>6)
    24   u8,u8    u1,v1      26 u16 tpage
    28   u8,u8    u2,v2      30 u8,u8 u3,v3
    32   FT4: u8[3] rgb + u8 0x2C
         GT4: 4 x (u8[3] rgb + u8 0x3C), un color por vertice

Uso:
    python3 tools/acemodel.py list <seccion.bin>            # modelos de la seccion
    python3 tools/acemodel.py verts <seccion.bin> <i>       # vertices del modelo i
    python3 tools/acemodel.py obj <seccion.bin> <i> <out>   # .obj con caras
    python3 tools/acemodel.py check <dir_bpb>               # validacion global
    python3 tools/acemodel.py tilemap <dir_bpb>             # base del indice del mapa
    python3 tools/acemodel.py normals <dir_bpb>             # h1/e0/e1 y normales (s5)
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
        pad = 12 if self.off_vertices == 0x18 else 0
        self.prims = []
        p1 = self._prims(self.off_prims, 36, 0x2C)
        if self.off_prims + 4 + (36 + pad) * len(p1) != self.off_end:
            raise ValueError("bloque 1 de primitivas incoherente con off_end")
        p2 = self._prims(self.off_end, 48, 0x3C)
        if self.off_end + 4 + (48 + pad) * len(p2) != len(raw):
            raise ValueError("bloque 2 de primitivas incoherente con el final")
        self.prims = p1 + p2

    def _prims(self, off, size, code):
        raw = self.raw
        n = struct.unpack_from("<I", raw, off)[0]
        out = []
        for k in range(n):
            a = off + 4 + size * k
            idx = tuple(raw[a + 16:a + 20])
            if raw[a + 35] != code or max(idx) >= len(self.vertices):
                raise ValueError("primitiva incoherente")
            for v, (oxy, oz) in zip(idx, zip(raw[a + 8:a + 16:2], raw[a + 9:a + 16:2])):
                if (oxy, oz) != (4 * (v % 3) + 20 * (v // 3), 12 + 2 * (v % 3) + 20 * (v // 3)):
                    raise ValueError("offsets GTE incoherentes con los indices")
            clut, tpage = struct.unpack_from("<H", raw, a + 22)[0], struct.unpack_from("<H", raw, a + 26)[0]
            uv = (raw[a + 20:a + 22], raw[a + 24:a + 26], raw[a + 28:a + 30], raw[a + 30:a + 32])
            out.append({"idx": idx, "clut": clut, "tpage": tpage, "uv": [tuple(t) for t in uv],
                        "gouraud": code == 0x3C, "h": (raw[a], raw[a + 1])})
        return out

    def faces(self):
        """Caras como tuplas de indices (quads en orden de contorno)."""
        out = []
        for p in self.prims:
            i0, i1, i2, i3 = p["idx"]
            out.append((i0, i1, i2) if i3 == i2 else (i0, i1, i3, i2))
        return out

    def real_bbox(self):
        xs, ys, zs = zip(*self.vertices)
        return (max(xs), min(xs), max(ys), min(ys), max(zs), min(zs))


def _vertex_block(raw, off):
    n = struct.unpack_from("<I", raw, off)[0]
    verts = []
    for g in range((n + 2) // 3):
        x0, y0, x1, y1, x2, y2, z0, z1, z2, _ = struct.unpack_from("<10h", raw, off + 4 + 20 * g)
        verts += [(x0, y0, z0), (x1, y1, z1), (x2, y2, z2)]
    return verts[:n], off + 4 + 20 * ((n + 2) // 3)


class LodModel:
    """Modelos con LOD de las secciones 2 y 3 del tipo 17 (aviones, armas).

    Cabecera: u16 0, u16 extent, u32 0x1C, u32 off_x2, u32 off_x4, u32 0,
              u32 off_gt3, u32 off_gt4.
    En 0x1C un bloque de vertices (mismo layout que Model). Si off_x2 y
    off_x4 valen 0x1C no hay mas; si no, son otros dos bloques con LOS MISMOS
    vertices a escala x2 y x4 (diferencias de redondeo <= 1), y el de x4
    alcanza exactamente `extent`. Los LOD sin bloques extra guardan los
    vertices a escala 1/4 (4*max|v| ~ extent).
    GT3: u32 n ; n x 32 B = u32 0, u8 i0,i1,i2,pad, 3 x (rgb + 0x34),
         uv0+CLUT, uv1+tpage, uv2+pad.
    GT4: u32 n ; n x 36 B = u32 0, u8 i0..i3, 4 x (rgb + 0x3C),
         uv0+CLUT, uv1+tpage, uv2, uv3.
    """

    def __init__(self, raw):
        if len(raw) < 32 or raw[:2] != b"\0\0":
            raise ValueError("no es un modelo")
        (self.extent, ov, self.off_x2, self.off_x4, unk,
         self.off_gt3, self.off_gt4) = struct.unpack_from("<HIIIIII", raw, 2)
        if ov != 0x1C or unk != 0:
            raise ValueError("cabecera de modelo LOD no reconocida")
        self.raw = raw
        base, end = _vertex_block(raw, 0x1C)
        self.scaled = self.off_x2 != 0x1C
        if self.scaled:
            x2, end2 = _vertex_block(raw, self.off_x2)
            x4, end4 = _vertex_block(raw, self.off_x4)
            if end != self.off_x2 or end2 != self.off_x4 or end4 != self.off_gt3:
                raise ValueError("bloques de vertices incoherentes")
            if not len(base) == len(x2) == len(x4):
                raise ValueError("bloques de escala con distinto numero de vertices")
            self.vertices = x4
        else:
            if end != self.off_gt3:
                raise ValueError("bloque de vertices incoherente con off_gt3")
            self.vertices = [(4 * x, 4 * y, 4 * z) for x, y, z in base]
        self.base_vertices = base
        self.prims = []
        n3 = struct.unpack_from("<I", raw, self.off_gt3)[0]
        n4 = struct.unpack_from("<I", raw, self.off_gt4)[0]
        if self.off_gt3 + 4 + 32 * n3 != self.off_gt4 or self.off_gt4 + 4 + 36 * n4 != len(raw):
            raise ValueError("bloques de primitivas incoherentes")
        for k in range(n3):
            a = self.off_gt3 + 4 + 32 * k
            self._prim(tuple(raw[a + 4:a + 7]), raw[a + 11] == raw[a + 15] == raw[a + 19] == 0x34, a + 20, a + 24)
        for k in range(n4):
            a = self.off_gt4 + 4 + 36 * k
            ok = all(raw[a + 11 + 4 * j] == 0x3C for j in range(4))
            self._prim(tuple(raw[a + 4:a + 8]), ok, a + 24, a + 28)

    def _prim(self, idx, code_ok, a_uv0, a_uv1):
        if not code_ok or max(idx) >= len(self.vertices):
            raise ValueError("primitiva incoherente")
        clut = struct.unpack_from("<H", self.raw, a_uv0 + 2)[0]
        tpage = struct.unpack_from("<H", self.raw, a_uv1 + 2)[0]
        self.prims.append({"idx": idx, "clut": clut, "tpage": tpage})

    def faces(self):
        out = []
        for p in self.prims:
            idx = p["idx"]
            out.append(idx if len(idx) == 3 else (idx[0], idx[1], idx[3], idx[2]))
        return out

    def real_bbox(self):
        xs, ys, zs = zip(*self.vertices)
        return (max(xs), min(xs), max(ys), min(ys), max(zs), min(zs))


def parse_model(raw):
    """Model o LodModel segun la cabecera."""
    if len(raw) >= 8 and struct.unpack_from("<I", raw, 4)[0] == 0x1C:
        return LodModel(raw)
    return Model(raw)


def models(buf):
    """[(indice, modelo)] de una seccion con tabla de offsets de modelos."""
    out = []
    for i, (a, b) in enumerate(offset_table(buf)):
        try:
            out.append((i, parse_model(buf[a:b])))
        except (ValueError, struct.error):
            pass
    return out


def write_obj(path, model):
    with open(path, "w") as fh:
        fh.write("# modelo de Ace Combat 3 (vertices y caras; sin UV todavia)\n")
        fh.write("# eje Y de PS1 hacia abajo: se invierte para visores estandar\n")
        for x, y, z in model.vertices:
            fh.write(f"v {x} {-y} {z}\n")
        for f in model.faces():
            fh.write("f " + " ".join(str(i + 1) for i in f) + "\n")


def edge_continuity(tilemap, tiles, base):
    """Mide si las teselas vecinas casan en sus bordes con indice `base`.

    Para cada par de celdas vecinas en horizontal, compara los vertices del
    borde derecho de una (x=1024) con los del borde izquierdo de la otra
    (x=-1024) que comparten z. Devuelve (coinciden, comparados).
    """
    def edge(m, x):
        return {v[2]: v[1] for v in m.vertices if v[0] == x}
    same = total = 0
    for gy in range(256):
        for gx in range(255):
            a = struct.unpack_from("<H", tilemap, (gy * 256 + gx) * 2)[0] & 0x1FF
            b = struct.unpack_from("<H", tilemap, (gy * 256 + gx + 1) * 2)[0] & 0x1FF
            ta, tb = tiles.get(a - base), tiles.get(b - base)
            if ta is None or tb is None or a == b:
                continue
            ea, eb = edge(ta, 1024), edge(tb, -1024)
            for z in ea.keys() & eb.keys():
                total += 1
                same += int(ea[z] == eb[z])
    return same, total


def cmd_tilemap(directory):
    """Indice de tesela del mapa (seccion 0) y significado de los bits altos.

    1. Base del indice: continuidad de bordes con base 0 y con base 1.
    2. Bits 9-11 de la celda: el render (0x8005FF9C) los usa para anular la
       reutilizacion de vertices ya transformados de la tesela vecina. Aqui se
       mide, para el borde izquierdo de cada celda, cuantos bordes no casan con
       la vecina segun cada bit este a 0 o a 1.
    """
    import collections
    from acerec import Record
    bits = collections.Counter()
    for name in sorted(os.listdir(directory)):
        rec = Record.from_file(os.path.join(directory, name))
        if rec is None or rec.type != 17:
            continue
        tm = rec.section(0)
        tiles = dict(models(rec.section(1)))
        res = [edge_continuity(tm, tiles, base) for base in (0, 1)]
        print(f"{name[:4]}: base0 {res[0][0]}/{res[0][1]}  base1 {res[1][0]}/{res[1][1]}")
        for gz in range(256):
            for gx in range(1, 256):
                a = struct.unpack_from("<H", tm, (gz * 256 + gx - 1) * 2)[0]
                b = struct.unpack_from("<H", tm, (gz * 256 + gx) * 2)[0]
                ta, tb = tiles.get((a & 0x1FF) - 1), tiles.get((b & 0x1FF) - 1)
                if not (a & 0x1FF) or not (b & 0x1FF) or ta is None or tb is None:
                    continue
                ea = {v[2]: v[1] for v in ta.vertices if v[0] == 1024}
                eb = {v[2]: v[1] for v in tb.vertices if v[0] == -1024}
                common = ea.keys() & eb.keys()
                if not common:
                    continue
                ok = all(ea[k] == eb[k] for k in common)
                for bit in (9, 10, 11):
                    bits[(bit, (b >> bit) & 1, ok)] += 1
    print("bordes izquierdos que NO casan con la vecina, segun los bits altos de la celda:")
    for bit in (9, 10, 11):
        for v in (0, 1):
            good, bad = bits[(bit, v, True)], bits[(bit, v, False)]
            print(f"  bit {bit} = {v}: {bad}/{good + bad} ({100 * bad / max(1, good + bad):.1f} %)")
    return 0


def cmd_normals(directory):
    """Valida h1/e0/e1 contra la tabla de normales (seccion 5) de cada registro.

    Semantica sacada del codigo de interseccion con el terreno de ACE.BIN
    (0x80097EDC, EU): e0/e1 son offsets a SVECTOR de normal; el bit 0x100
    (h1 = 1) es triangulo y el 0x200 (h1 = 2) cuadrilatero partido.
    """
    import collections
    import math
    from acerec import Record

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    def sub(a, b):
        return tuple(x - y for x, y in zip(a, b))

    def cos(n, m):
        ln = math.sqrt(sum(x * x for x in n))
        lm = math.sqrt(sum(x * x for x in m))
        return sum(x * y for x, y in zip(n, m)) / (ln * lm) if ln and lm else 0.0

    c = collections.Counter()
    for name in sorted(os.listdir(directory)):
        rec = Record.from_file(os.path.join(directory, name))
        if rec is None or rec.type != 17:
            continue
        s5 = rec.section(5)
        top = 0
        for k in (1, 4):
            for _, m in models(rec.section(k)):
                if not isinstance(m, Model):
                    continue
                raw, v = m.raw, m.vertices
                for blk, size in ((m.off_prims, 36), (m.off_end, 48)):
                    for j in range(struct.unpack_from("<I", raw, blk)[0]):
                        a = blk + 4 + size * j
                        h1 = raw[a + 1]
                        e0, e1 = struct.unpack_from("<HH", raw, a + 4)
                        i0, i1, i2, i3 = raw[a + 16:a + 20]
                        c["primitivas"] += 1
                        c["e multiplo de 8 y dentro de s5"] += int(e0 % 8 == 0 and e1 % 8 == 0 and max(e0, e1) + 8 <= len(s5))
                        top = max(top, e0, e1)
                        tri = i3 == i2
                        c[f"h1={h1} {'triangulo' if tri else 'cuadrilatero'}"] += 1
                        if h1 != 2:
                            c["h1!=2 con e0==e1"] += int(e0 == e1)
                            n0 = struct.unpack_from("<3h", s5, e0)
                            cr = cross(sub(v[i1], v[i0]), sub(v[i2], v[i0]))
                            if any(cr):
                                c["caras planas comprobadas"] += 1
                                c["  normal antiparalela (cos < -0.99)"] += int(cos(n0, cr) < -0.99)
                        elif not tri:
                            n0 = struct.unpack_from("<3h", s5, e0)
                            n1 = struct.unpack_from("<3h", s5, e1)
                            ok = (cos(n0, cross(sub(v[i1], v[i0]), sub(v[i2], v[i0]))) < -0.98 and
                                  cos(n1, cross(sub(v[i3], v[i1]), sub(v[i2], v[i1]))) < -0.98)
                            c["h1=2: particion (i0,i1,i2)+(i1,i3,i2) casa"] += int(ok)
        c["registros"] += 1
        c["max(e)+8 == len(s5)"] += int(top + 8 == len(s5))
    for key, n in c.items():
        print(f"{key}: {n}")
    return 0


def cmd_check(directory):
    from acerec import Record
    total = size_ok = bbox_n = bbox_ok = lod_n = lod_ok = 0
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
                if struct.unpack_from("<I", raw, 4)[0] not in (0x18, 0x1C, 0x24):
                    continue
                total += 1
                try:
                    m = parse_model(raw)
                except (ValueError, struct.error):
                    continue
                size_ok += 1
                if isinstance(m, LodModel):
                    lod_n += 1
                    if m.scaled:
                        mx = max(max(map(abs, v)) for v in m.vertices)
                        lod_ok += int(mx == m.extent)
                    else:
                        mx = max(max(map(abs, v)) for v in m.base_vertices)
                        lod_ok += int(abs(4 * mx - m.extent) <= 4)
                    continue
                if m.bbox:
                    bbox_n += 1
                    bbox_ok += int(m.bbox == m.real_bbox())
    print(f"modelos candidatos: {total}")
    print(f"vertices + primitivas coherentes (bloques consumidos exactos,")
    print(f"  codigos GPU, indices y offsets GTE): {size_ok}/{total}")
    print(f"bbox de cabecera == bbox de vertices (objetos): {bbox_ok}/{bbox_n}")
    print(f"modelos LOD con extent coherente con su escala: {lod_ok}/{lod_n}")
    return 0 if size_ok == total and bbox_ok == bbox_n and lod_ok == lod_n else 1


def main(argv):
    if len(argv) >= 3 and argv[1] == "check":
        return cmd_check(argv[2])
    if len(argv) >= 3 and argv[1] == "normals":
        return cmd_normals(argv[2])
    if len(argv) >= 3 and argv[1] == "tilemap":
        return cmd_tilemap(argv[2])
    if len(argv) >= 3 and argv[1] == "list":
        buf = open(argv[2], "rb").read()
        for i, m in models(buf):
            extra = f" bbox={m.bbox}" if getattr(m, "bbox", None) else ""
            if isinstance(m, LodModel):
                extra = " LOD " + ("escalas x1/x2/x4" if m.scaled else "escala 1/4")
            print(f"{i:4d} extent={m.extent:5d} vertices={len(m.vertices):4d} prims={len(m.prims):4d}{extra}")
        return 0
    if len(argv) >= 4 and argv[1] in ("verts", "obj"):
        buf = open(argv[2], "rb").read()
        i = int(argv[3])
        a, b = offset_table(buf)[i]
        m = parse_model(buf[a:b])
        if argv[1] == "verts":
            print(f"modelo {i}: {len(m.vertices)} vertices, bbox real {m.real_bbox()}")
            for j, v in enumerate(m.vertices):
                print(f"  v{j:<4d} x={v[0]:6d} y={v[1]:6d} z={v[2]:6d}")
        else:
            write_obj(argv[4], m)
            print(f"{argv[4]}: {len(m.vertices)} vertices, {len(m.faces())} caras")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
