#!/usr/bin/env python3
"""psxcheck_mdec — compara el MDEC de psx/ con el decodificador mdec de libav (FFmpeg).

Referencia independiente: avdec_mdec de gst-libav. Para cada fotograma elegido de las dos
peliculas del ACE.SPB: DecDCTvlc2 de psx/ (identica a la original, tools/psyqcheck_press.py)
da los coeficientes; se reescriben como flujo de version 2 (la que libav decodifica sin
dudas; mismos coeficientes) en un archivo STR estandar de un solo fotograma, y libav lo
decodifica a YUV 4:2:0. Del lado de psx/: DecDCTin (24 bits) + DecDCTout, montando los
macrobloques por columnas como el juego. La salida YUV de libav se pasa a RGB con la
MISMA conversion de color del MDEC de psx/ (rango completo; la de libav a RGB es de rango
limitado y no sirve de referencia), asi que se compara la descuantizacion y la IDCT.
Criterio: PSNR >= 45 dB en cada fotograma (la IDCT de libav es de coma flotante; la del
MDEC, de coma fija).

Uso:
    python3 tools/psxcheck_mdec.py <imagen.bin> [fotogramas_por_pelicula]
"""
import ctypes
import math
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psyqcheck_gpu import build  # noqa: E402
from psyqcheck_press import movie_frames  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import re
AC={}
for code,ln,run,lev in re.findall(r'\{0x([0-9A-Fa-f]+), (\d+), (\d+), (\d+)\}', open(os.path.join(ROOT, 'psx', 'src', 'libpress.c')).read()):
    AC[(int(run),int(lev))]=(int(code,16),int(ln))
class W:
    def __init__(s): s.words=[]; s.acc=0; s.n=0
    def put(s,v,k):
        for i in range(k-1,-1,-1):
            s.acc=(s.acc<<1)|((v>>i)&1); s.n+=1
            if s.n==16: s.words.append(s.acc); s.acc=0; s.n=0
    def done(s):
        while s.n: s.put(0,1)
        return struct.pack('<%dH'%len(s.words),*s.words)
def rl_to_v2(rl):
    """rl: salida de DecDCTvlc2 (cabecera + codigos). Devuelve un flujo de version 2"""
    hdr=struct.unpack_from('<I',rl,0)[0]; count=hdr&0xFFFF
    codes=struct.unpack_from('<%dH'%(count*2),rl,4)
    w=W(); q=None; i=0; nb=0
    while i<len(codes):
        c=codes[i]
        if c==0xFE00 and nb==0: i+=1; continue   # relleno final
        if nb==0:   # DC
            q=c>>10; w.put(c&0x3FF,10); nb=1; i+=1; continue
        if c==0xFE00: w.put(2,2); nb=0; i+=1; continue
        run,lev=c>>10,c&0x3FF
        sl=lev-1024 if lev&0x200 else lev
        key=(run,abs(sl))
        if key in AC and sl!=0:
            code,ln=AC[key]; w.put(code,ln); w.put(1 if sl<0 else 0,1)
        else:
            w.put(1,6); w.put(run,6); w.put(lev,10)
        i+=1
    body=w.done()
    return struct.pack('<HHHH',count,0x3800,q or 1,2)+body


def c8(v):
    return -128 if v < -128 else 127 if v > 127 else v


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    image = sys.argv[1]
    nper = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    lib = build()
    ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
    base = ctypes.addressof(ram)
    lib.DecDCTReset(0)
    worst, bad = 99.0, 0
    with tempfile.TemporaryDirectory() as tmp:
        for e in (0, 1):
            res = []
            for fn, w, h, data in movie_frames(image, e, nper, step=40):
                ctypes.memmove(base + 0x100000, data, len(data))
                lib.DecDCTvlc2(ctypes.c_void_p(base + 0x100000), ctypes.c_void_p(base + 0x120000), None)
                count = struct.unpack_from('<H', data, 0)[0]
                rl = bytes(ram[0x120000:0x120000 + 4 + count * 4])
                v2 = rl_to_v2(rl)
                chunks = [v2[i:i + 2016].ljust(2016, b'\0') for i in range(0, len(v2), 2016)]
                src = os.path.join(tmp, 'f.str')
                with open(src, 'wb') as out:
                    for rep in range(2):   # dos copias: libav no siempre entrega el ultimo fotograma
                        for ci, ch in enumerate(chunks):
                            raw = bytearray(2352)
                            raw[0:12] = b'\x00' + b'\xff' * 10 + b'\x00'
                            raw[15] = 2
                            raw[16:24] = bytes([1, 0, 0x48, 0]) * 2
                            raw[24:56] = struct.pack('<HHHHIIHH', 0x0160, 0x8001, ci, len(chunks), rep + 1, len(v2),
                                                     w, h) + bytes(12)
                            raw[56:56 + 2016] = ch
                            out.write(raw)
                for p in os.listdir(tmp):
                    if p.endswith('.yuv'):
                        os.remove(os.path.join(tmp, p))
                subprocess.run(['gst-launch-1.0', '-q', 'filesrc', 'location=' + src, '!', 'avdemux_psxstr', '!',
                                'avdec_mdec', '!', 'video/x-raw,format=I420', '!', 'multifilesink',
                                'location=' + os.path.join(tmp, 'y_%03d.yuv')], check=True, capture_output=True)
                yuv = open(os.path.join(tmp, 'y_000.yuv'), 'rb').read()
                Y, U, V = yuv[:w * h], yuv[w * h:w * h * 5 // 4], yuv[w * h * 5 // 4:]
                ctypes.memmove(base + 0x120000, rl, len(rl))
                lib.DecDCTin(ctypes.c_void_p(base + 0x120000), 1)
                mbw, mbh = (w + 15) // 16, (h + 15) // 16
                lib.DecDCTout(ctypes.c_void_p(base + 0x150000), mbw * mbh * 192)
                o = bytes(ram[0x150000:0x150000 + mbw * mbh * 768])
                se = 0
                for mx in range(mbw):
                    for my in range(mbh):
                        mb = o[(mx * mbh + my) * 768:(mx * mbh + my + 1) * 768]
                        for py in range(16):
                            yy = my * 16 + py
                            for px in range(16):
                                xx = mx * 16 + px
                                if yy >= h or xx >= w:
                                    continue
                                cb, cr = U[(yy // 2) * (w // 2) + xx // 2] - 128, V[(yy // 2) * (w // 2) + xx // 2] - 128
                                lum = Y[yy * w + xx] - 128
                                ref = (c8(lum + ((1436 * cr) >> 10)) + 128, c8(lum + ((-352 * cb - 731 * cr) >> 10)) + 128,
                                       c8(lum + ((1815 * cb) >> 10)) + 128)
                                k = (py * 16 + px) * 3
                                se += sum((mb[k + c] - ref[c]) ** 2 for c in range(3))
                mse = se / (w * h * 3)
                p = 10 * math.log10(255 * 255 / mse) if mse else 99.0
                res.append(p)
                worst = min(worst, p)
                bad += p < 45
            print('pelicula %d (%dx%d): PSNR %s dB' % (e, w, h, ' '.join('%.1f' % x for x in res)))
    print('peor %.1f dB; %s' % (worst, 'TODO BIEN' if not bad else 'HAY FALLOS (%d fotogramas < 45 dB)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
