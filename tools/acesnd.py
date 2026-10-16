"""acesnd — bancos de sonido del SPU (tipo 4 seccion 2, tipo 17 seccion 15).

FORMATO (verificado en 63/63 bancos del disco europeo y 63/63 del americano):

    u8   b0;           // PRIMERA RANURA de la tabla de sonidos (FUN_8002D544):
                       // 0 comun (0000 s3, 76), 0x54 avion, 0x58 mision; id = b0 + i
    u8   n;            // numero de entradas
    u16  b2;           // 0x0d en tipo 4, 0x67 en tipo 17      SIN RESOLVER
    u32  spu_base;     // direccion de destino en la RAM del SPU (512 KB)
    u32  size;         // bytes de ADPCM, multiplo de 16
    struct {           // 20 bytes
        u32 id;        // 1, 2... / 0xFF, 0x105...              SIN RESOLVER
        u16 p0, p1;    // volumen izquierdo / derecho de la voz (FUN_8002E0F8)
        u16 vol;       // NOTA DE REPRODUCCION (nota << 8 | fraccion; 0x3C00 = 60)
        u8  u0;        // fraccion de la nota base
        u8  note;      // nota base de la muestra (SpuVoiceAttr.sample_note = note << 8 | u0)
        u32 adsr;      // registros ADSR1/ADSR2 de la voz (0x5FC980FF el comun)
        u32 addr;      // direccion de la muestra en la RAM del SPU
    } entry[n];
    u8   adpcm[size];  // se sube tal cual a spu_base
    // relleno de ceros hasta el final de la seccion

ADPCM del SPU: bloques de 16 bytes. byte 0 = filtro (bits 4-7, 0..4) y
desplazamiento (bits 0-3, 0..12); byte 1 = flags (bit 0 fin de muestra,
bit 1 repetir, bit 2 inicio de bucle); 14 bytes = 28 muestras de 4 bits.

Pruebas de la lectura, en los 126 bancos:
  - 12 + 20*n + size cabe en la seccion y el resto son ceros
  - todos los bloques de los datos tienen filtro <= 4, desplazamiento <= 12
  - el ultimo bloque lleva el flag de fin
  - cada addr - spu_base esta alineado a 16 y el bloque anterior lleva flag
    de fin (las muestras empiezan justo donde acaba otra)

Consecuencia: los valores que cambian entre EU y US en estas secciones
(0x043C90 / 0x0496D0) son spu_base: el reparto de la RAM de sonido, NO
punteros a codigo de ACE.BIN como se supuso.

Frecuencia (resuelta): el SPU reproduce a 44100 * 2^((nota - nota_base)/12) Hz,
con nota = vol/256 y nota_base = note + u0/256. En los 963 sonidos de los bancos,
436 dan exactamente 11025 o 22050 Hz. --rate fuerza otra frecuencia.

Uso:
    python3 tools/acesnd.py list <seccion.bin>
    python3 tools/acesnd.py wav  <seccion.bin> <dir> [--rate N]
    python3 tools/acesnd.py check <dir_bpb>
"""
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
SECTION = {4: 2, 17: 15}


class SoundBank:
    def __init__(self, buf):
        a, self.spu_base, self.size = struct.unpack_from("<3I", buf, 0)
        self.n = (a >> 8) & 0xFF
        self.b0, self.b2 = a & 0xFF, a >> 16
        self.data_off = 12 + 20 * self.n
        if self.size % 16 or self.data_off + self.size > len(buf):
            raise ValueError("cabecera de banco incoherente")
        self.entries = []
        for i in range(self.n):
            eid, p0, p1, vol, u0, note, adsr, addr = struct.unpack_from("<IHHHBBII", buf, 12 + 20 * i)
            self.entries.append({"id": eid, "p0": p0, "p1": p1, "vol": vol, "u0": u0,
                                 "note": note, "adsr": adsr, "off": addr - self.spu_base})
        self.data = buf[self.data_off:self.data_off + self.size]
        self.tail = buf[self.data_off + self.size:]

    def problems(self):
        """Lista de incoherencias; vacia si el banco valida."""
        out = []
        d = self.data
        for i in range(0, self.size, 16):
            if d[i] >> 4 > 4 or d[i] & 15 > 12 or d[i + 1] & ~7:
                out.append(f"bloque invalido en {i:#x}")
                break
        if self.size and not d[-15] & 1:
            out.append("el ultimo bloque no lleva flag de fin")
        for i, e in enumerate(self.entries):
            s = e["off"]
            if not 0 <= s < self.size or s % 16:
                out.append(f"entrada {i}: direccion fuera de los datos")
            elif s and not d[s - 15] & 1:
                out.append(f"entrada {i}: no empieza tras un fin de muestra")
        if any(self.tail):
            out.append("bytes no nulos tras los datos")
        return out

    def samples(self):
        """[(offset, fin)] de cada muestra distinta, en orden de direccion."""
        starts = sorted({e["off"] for e in self.entries})
        out = []
        for s in starts:
            o = s
            while o < self.size and not self.data[o + 1] & 1:
                o += 16
            out.append((s, min(o + 16, self.size)))
        return out


def decode_adpcm(data, start, end):
    """ADPCM del SPU a PCM s16. Devuelve (muestras, recortadas)."""
    pcm = []
    s1 = s2 = 0
    clipped = 0
    for o in range(start, end, 16):
        f, shift = data[o] >> 4, data[o] & 15
        k0, k1 = FILTERS[f]
        for b in data[o + 2:o + 16]:
            for nib in (b & 15, b >> 4):
                v = (nib - 16 if nib >= 8 else nib) << 12
                v = (v >> shift) + ((s1 * k0 + s2 * k1 + 32) >> 6)
                if v > 32767 or v < -32768:
                    clipped += 1
                    v = max(-32768, min(32767, v))
                pcm.append(v)
                s2, s1 = s1, v
    return pcm, clipped


def write_wav(path, pcm, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))


def cmd_check(directory):
    from acerec import Record
    ok = total = 0
    clipped = nsamp = 0
    for name in sorted(os.listdir(directory)):
        rec = Record.from_file(os.path.join(directory, name))
        if rec is None or rec.type not in SECTION:
            continue
        total += 1
        try:
            bank = SoundBank(rec.section(SECTION[rec.type]))
        except ValueError as e:
            print(f"{name}: {e}")
            continue
        probs = bank.problems()
        if probs:
            print(f"{name}: {'; '.join(probs)}")
            continue
        ok += 1
        for s, e in bank.samples():
            pcm, c = decode_adpcm(bank.data, s, e)
            clipped += c
            nsamp += len(pcm)
    print(f"bancos de sonido coherentes: {ok}/{total}")
    print(f"muestras PCM decodificadas: {nsamp}, recortadas: {clipped} ({100 * clipped / max(1, nsamp):.3f} %)")
    return 0 if ok == total else 1


def main(argv):
    if len(argv) >= 3 and argv[1] == "check":
        return cmd_check(argv[2])
    if len(argv) >= 3 and argv[1] in ("list", "wav"):
        bank = SoundBank(open(argv[2], "rb").read())
        if argv[1] == "list":
            print(f"spu_base={bank.spu_base:#x} size={bank.size} entradas={bank.n} "
                  f"b0={bank.b0:#x} b2={bank.b2:#x}")
            for i, e in enumerate(bank.entries):
                print(f"  {i:3d} id={e['id']:#5x} p={e['p0']:#06x}/{e['p1']:#06x} vol={e['vol']:#06x} "
                      f"u0={e['u0']:3d} nota={e['note']:3d} adsr={e['adsr']:#010x} off={e['off']:#x}")
            for s, e in bank.samples():
                print(f"  muestra {s:#08x}-{e:#08x}: {(e - s) // 16 * 28} muestras PCM")
            probs = bank.problems()
            print("coherencia:", "OK" if not probs else "; ".join(probs))
            return 0
        rate = int(argv[argv.index("--rate") + 1]) if "--rate" in argv else 22050
        os.makedirs(argv[3], exist_ok=True)
        stem = os.path.splitext(os.path.basename(argv[2]))[0]
        for k, (s, e) in enumerate(bank.samples()):
            pcm, _ = decode_adpcm(bank.data, s, e)
            out = os.path.join(argv[3], f"{stem}_{k:02d}.wav")
            write_wav(out, pcm, rate)
            print(f"{out}  {len(pcm)} muestras")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
