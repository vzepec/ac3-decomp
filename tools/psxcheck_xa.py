#!/usr/bin/env python3
"""psxcheck_xa — valida el decodificador XA-ADPCM de psx/ contra libav (FFmpeg).

Referencia independiente: el decodificador adpcm_xa de libav, que en este sistema esta
disponible a traves de GStreamer (gst-libav: avdemux_psxstr + avdec_adpcm_xa); no hace
falta instalar ffmpeg. Para cada entrada elegida de ACE.SPB se separan los sectores de
audio de su canal (las entradas XA van entrelazadas, 8 canales), se escriben como un
archivo de sectores crudos, libav los decodifica a WAV y la decodificacion de psx/
(psx_xa_decode) debe coincidir muestra a muestra. Se eligen entradas que cubran todas
las combinaciones de codificacion (mono/estereo, 37800/18900 Hz, 4/8 bits) presentes.

Ademas, con el CODIGO DEL JUEGO en el interprete (como tools/psxcheck_io.py, con libcd,
libetc y libspu desviadas a psx/): packs_init, una pista de musica con spb_stream_start
(modo 2: Setfilter, Setmode 0xE8, ReadS) y cd_state_machine (0x80029E8C) en cada
fotograma, con la SPU generando audio. Los sectores que la capa entrega a la SPU deben
ser exactamente los de audio del canal de la pista, en orden; la pista debe acabar
(callback de fin) y la salida de la SPU debe llevar la musica (entrada de CD activada
por el propio juego).

Uso:
    python3 tools/psxcheck_xa.py <imagen.bin> [max_entradas]
    XA_ENTRY=n (pista del SPH, 3 por defecto) XA_WAV=salida.wav (primer minuto de la SPU)
"""
import ctypes
import os
import struct
import subprocess
import sys
import tempfile
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acepack import load, spb_tag  # noqa: E402
from psxiso import Disc  # noqa: E402
from psyqcheck_gpu import build  # noqa: E402


def channel_sectors(f, start, count, chan):
    out = []
    for k in range(count):
        f.seek((start + k) * 2352)
        raw = f.read(2352)
        if len(raw) == 2352 and raw[15] == 2 and raw[17] == chan and raw[18] & 4:
            out.append(raw)
    return out


def libav_decode(sectors, tmp):
    src, dst = os.path.join(tmp, 'in.str'), os.path.join(tmp, 'out.wav')
    open(src, 'wb').write(b''.join(sectors))
    subprocess.run(['gst-launch-1.0', '-q', 'filesrc', 'location=' + src, '!', 'avdemux_psxstr', '!',
                    'avdec_adpcm_xa', '!', 'audioconvert', '!', 'audio/x-raw,format=S16LE', '!', 'wavenc', '!',
                    'filesink', 'location=' + dst], check=True, capture_output=True)
    w = wave.open(dst)
    data = w.readframes(w.getnframes())
    return list(struct.unpack('<%dh' % (len(data) // 2), data)), w.getnchannels(), w.getframerate()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    image = sys.argv[1]
    maxn = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    lib = build()
    disc = Disc(image)
    idx, pack = load(disc, 'SPB')
    f = open(image, 'rb')
    # una entrada por codificacion distinta (mas algunas extra), con su canal
    chosen, seen = [], set()
    for i, (tag, sec) in enumerate(idx.entries):
        t = spb_tag(tag)
        f.seek((pack.lba + sec) * 2352)
        for k in range(16):
            raw = f.read(2352)
            if raw[15] == 2 and raw[17] == t['channel'] and raw[18] & 4:
                key = (raw[19] & 0x3F, t['mode'])
                if key not in seen or len(chosen) < maxn:
                    seen.add(key)
                    chosen.append((i, t, raw[19]))
                break
        if len(chosen) >= maxn and len(seen) >= 1 and i > 60:
            break
    bad = 0
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        for i, t, ci in chosen[:max(maxn, len(seen))]:
            secs = channel_sectors(f, pack.lba + idx.entries[i][1], min(t['sectors'], 4000), t['channel'])
            ref, nch, rate = libav_decode(secs, tmp)
            x = ctypes.create_string_buffer(64)
            lib.psx_xa_reset(x)
            out = (ctypes.c_int16 * 4032)()
            st, rt = ctypes.c_int(), ctypes.c_int()
            mine = []
            for raw in secs:
                n = lib.psx_xa_decode(x, raw, out, ctypes.byref(st), ctypes.byref(rt))
                mine += out[:n * (2 if st.value else 1)]
            same = mine == ref
            bad += not same
            total += len(mine)
            nd = sum(a != b for a, b in zip(mine, ref)) + abs(len(mine) - len(ref))
            print('entrada %3d (modo %d, canal %d, codificacion %02x: %s, %d Hz): %4d sectores, %8d muestras: %s'
                  % (i, t['mode'], t['channel'], ci, 'estereo' if st.value else 'mono', rt.value, len(secs), len(mine),
                     'IDENTICO a libav' if same else '%d DISTINTAS (libav %d canales, %d Hz, %d muestras)'
                     % (nd, nch, rate, len(ref))))
    print('%d muestras comparadas; %s' % (total, 'TODO IDENTICO' if not bad else 'HAY DIFERENCIAS'))
    bad += check_game(image, lib, idx, pack, f, int(os.environ.get('XA_ENTRY', '3')), os.environ.get('XA_WAV'))
    print('TODO BIEN' if not bad else 'HAY FALLOS (%d)' % bad)
    return 1 if bad else 0


def check_game(image, lib, idx, pack, f, k, wav=None):
    import psxcheck_io as io
    import psxcheck_snd as snd
    h = io.Harness(image, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data', 'exe',
                                       'ace_eu.bin'))
    h.hook_all()
    m, lib = h.m, h.lib
    for addr, (name, sig) in snd.LIB.items():   # libspu real (psxcheck_io la deja sin efecto)
        fn = getattr(lib, name)
        fn.restype = ctypes.c_int32

        def hk(mm, fn=fn, sig=sig):
            a = [ctypes.c_void_p(h.base + (mm.r[4 + j] & 0x1FFFFF)) if t == 'p' else
                 ctypes.c_int32(struct.unpack('<i', struct.pack('<I', mm.r[4 + j]))[0]) for j, t in enumerate(sig)]
            return fn(*a) or 0
        m.hooks[addr] = hk
    lib.psx_spu.restype = ctypes.c_size_t
    lib.SpuInit()
    lib.psx_spu_write16(ctypes.c_void_p(lib.psx_spu()), 0x180, 0x3FFF)   # volumen principal
    lib.psx_spu_write16(ctypes.c_void_p(lib.psx_spu()), 0x182, 0x3FFF)
    lib.psx_spu_write16(ctypes.c_void_p(lib.psx_spu()), 0x1B0, 0x7FFF)   # volumen de CD
    lib.psx_spu_write16(ctypes.c_void_p(lib.psx_spu()), 0x1B2, 0x7FFF)
    lib.psx_audio_connect()
    seen = []
    sink_t = ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_uint8))

    def sink(p):
        raw = ctypes.string_at(p, 2352)
        seen.append(raw[12:24])
        lib.psx_xa_to_spu(raw)
    cb = sink_t(sink)
    lib.psx_cd_set_xa_sink(cb)
    lib.CdInit()
    m.sw(io.FRAMES, 0)
    m.call(io.PACKS_INIT, [io.FRAMES])
    entry = m.call(io.TOC_ENTRY, [k | 0x800]) & 0xFFFFFFFF
    tag, sec = m.lw(entry), m.lw(entry + 4)
    t = spb_tag(tag)
    want = [raw[12:24] for raw in channel_sectors(f, sec, t['sectors'], t['channel'])]
    h.done = None
    r = m.call(io.STREAM_START, [entry, 0, 0, io.DONE_CB, 0])
    spu = ctypes.c_void_p(lib.psx_spu())
    out = (ctypes.c_int16 * (2 * 882))()
    pcm = bytearray()
    peak = frames = 0
    while h.done is None and frames < 60 * 50 * 10:
        lib.psx_vblank()
        m.sw(io.FRAMES, m.lw(io.FRAMES) + 1)
        m.call(0x80029E8C, [])
        lib.psx_spu_render(spu, out, 882)
        peak = max(peak, max(abs(x) for x in out[::7]))
        if wav is not None and len(pcm) < 44100 * 4 * 60:
            pcm += bytes(out)
        frames += 1
    ok = r == 1 and h.done == 0 and seen == want and peak > 1000
    print('juego: pista SPH %d (canal %d, %d sectores): inicio %s, fin %s en %d fotogramas (%.1f s), %d/%d sectores de '
          'audio entregados a la SPU %s, pico %d: %s'
          % (k, t['channel'], t['sectors'], r, h.done, frames, frames / 50, len(seen), len(want),
             'en orden' if seen == want else 'DISTINTOS', peak, 'bien' if ok else 'MAL'))
    if wav is not None:
        import wave as wv
        with wv.open(wav, 'wb') as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100); w.writeframes(bytes(pcm))
        print('audio: %s' % wav)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
