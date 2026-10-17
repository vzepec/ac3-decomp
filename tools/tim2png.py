"""tim2png — imagenes TIM de PlayStation a PNG, sin dependencias.

Las texturas de Ace Combat 3 SI son TIM estandar, pero viven dentro de
secciones comprimidas con ULZ (tools/ulz.py). Una busqueda de TIM sobre los
registros en crudo no las encuentra; sobre las secciones descomprimidas, si.

FORMATO TIM (Sony, documentado en el SDK):

    u32 magic = 0x10
    u32 flags         bits 0-2: modo (0=4bpp, 1=8bpp, 2=16bpp, 3=24bpp)
                      bit 3: hay CLUT
    [bloque CLUT, si flags & 8]
    bloque imagen

    bloque:  u32 bnum   (tamano del bloque, cabecera incluida = 12 + 2*w*h)
             u16 dx, dy (destino en VRAM, en unidades de 16 bits)
             u16 w, h   (en unidades de 16 bits)
             u16 data[w*h]

Validacion del escaneo: un candidato solo se acepta si `bnum == 12 + 2*w*h`
en todos sus bloques y el destino cabe en la VRAM (1024x512). Es una
condicion muy estricta; los datos aleatorios no la cumplen.

Color de 16 bits: bit 15 = STP, bits 10-14 azul, 5-9 verde, 0-4 rojo.
El color 0x0000 es transparente en el GPU; aqui se exporta con alfa 0.

Uso:
    python3 tools/tim2png.py scan <archivo...>          # listar TIM encontrados
    python3 tools/tim2png.py png <archivo> <dir> [clut] # volcar todos a PNG
"""
import os
import struct
import sys
import zlib

TIM_MAGIC = 0x10
BPP = {0: 4, 1: 8, 2: 16, 3: 24}


def _block(buf, off):
    if off + 12 > len(buf):
        return None
    bnum, dx, dy, w, h = struct.unpack_from("<IHHHH", buf, off)
    if w == 0 or h == 0 or bnum != 12 + 2 * w * h or off + bnum > len(buf):
        return None
    if dx + w > 1024 or dy + h > 512:
        return None
    return {"bnum": bnum, "dx": dx, "dy": dy, "w": w, "h": h, "data": off + 12}


def parse(buf, off=0):
    """Devuelve un dict con la TIM en `off`, o None si no es valida."""
    if off + 8 > len(buf):
        return None
    magic, flags = struct.unpack_from("<II", buf, off)
    if magic != TIM_MAGIC or flags & ~0xF or (flags & 7) not in BPP:
        return None
    mode = flags & 7
    p = off + 8
    clut = None
    if flags & 8:
        clut = _block(buf, p)
        if clut is None:
            return None
        p += clut["bnum"]
    elif mode in (0, 1):
        return None  # 4/8bpp sin CLUT: no lo hemos visto, no se acepta
    img = _block(buf, p)
    if img is None:
        return None
    return {"off": off, "end": p + img["bnum"], "bpp": BPP[mode], "clut": clut, "img": img}


def scan(buf):
    """Busca TIM validas alineadas a 4 bytes."""
    out = []
    off = 0
    while off + 8 <= len(buf):
        if buf[off] == TIM_MAGIC and buf[off + 1:off + 4] == b"\0\0\0":
            t = parse(buf, off)
            if t:
                out.append(t)
                off = t["end"]
                continue
        off += 4
    return out


def rgba5551(c, black_transparent=True):
    r = (c & 0x1F) << 3
    g = ((c >> 5) & 0x1F) << 3
    b = ((c >> 10) & 0x1F) << 3
    a = 0 if (c == 0 and black_transparent) else 255
    return bytes((r | r >> 5, g | g >> 5, b | b >> 5, a))


def to_rgba(buf, t, clut_row=0):
    """Devuelve (ancho_px, alto, bytes RGBA)."""
    img = t["img"]
    w16, h = img["w"], img["h"]
    data = buf[img["data"]:img["data"] + 2 * w16 * h]
    bpp = t["bpp"]
    pal = None
    if t["clut"]:
        c = t["clut"]
        row = min(clut_row, c["h"] - 1)
        base = c["data"] + row * c["w"] * 2
        n = c["w"]
        pal = [rgba5551(v) for v in struct.unpack_from(f"<{n}H", buf, base)]
    px = bytearray()
    if bpp == 4:
        width = w16 * 4
        for b in data:
            px += pal[b & 0xF] + pal[b >> 4]
    elif bpp == 8:
        width = w16 * 2
        for b in data:
            px += pal[b]
    elif bpp == 16:
        width = w16
        for (v,) in struct.iter_unpack("<H", data):
            px += rgba5551(v)
    else:
        width = (w16 * 2) // 3
        for y in range(h):
            row = data[y * w16 * 2:y * w16 * 2 + width * 3]
            for x in range(width):
                px += row[x * 3:x * 3 + 3] + b"\xff"
    return width, h, bytes(px)


def write_png(path, width, height, rgba):
    def chunk(kind, payload):
        c = struct.pack(">I", len(payload)) + kind + payload
        return c + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    stride = width * 4
    raw = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


def describe(t):
    i = t["img"]
    s = f"@{t['off']:#07x} {t['bpp']:>2}bpp img {i['w']}x{i['h']} (u16) en VRAM ({i['dx']},{i['dy']})"
    if t["clut"]:
        c = t["clut"]
        s += f"  clut {c['w']}x{c['h']} en ({c['dx']},{c['dy']})"
    return s


def main(argv):
    if len(argv) >= 3 and argv[1] == "scan":
        for path in argv[2:]:
            buf = open(path, "rb").read()
            tims = scan(buf)
            if tims:
                print(f"{os.path.basename(path)}: {len(tims)} TIM")
                for t in tims:
                    print("  " + describe(t))
        return 0
    if len(argv) >= 4 and argv[1] == "png":
        buf = open(argv[2], "rb").read()
        clut_row = int(argv[4]) if len(argv) > 4 else 0
        os.makedirs(argv[3], exist_ok=True)
        stem = os.path.splitext(os.path.basename(argv[2]))[0]
        for n, t in enumerate(scan(buf)):
            w, h, px = to_rgba(buf, t, clut_row)
            out = os.path.join(argv[3], f"{stem}_tim{n:02d}.png")
            write_png(out, w, h, px)
            print(f"{out}  {w}x{h}  {describe(t)}")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
