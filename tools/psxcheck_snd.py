#!/usr/bin/env python3
"""psxcheck_snd — valida la SPU por software y libspu de psx/ con datos y codigo del juego.

  1. Decodificacion de la SPU frente a tools/acesnd.py: cada muestra del banco comun
     (registro 0000, seccion 3) se sube a la RAM de sonido, se toca en una voz a tono
     0x1000 (una muestra de salida por muestra de ADPCM) con la envolvente al maximo, y la
     salida debe ser exactamente la decodificacion independiente de acesnd escalada por los
     volumenes (con el retardo de una muestra de la interpolacion; la ultima muestra no
     sale porque la voz se apaga al acabar el bloque final). Al final de la muestra
     la voz debe pararse (o repetir, si el ultimo bloque lo pide) y marcar ENDX.
  2. Driver de sonido del juego en el interprete, con sus llamadas a libspu desviadas a
     psx/ (como tools/psxcheck_io.py):
       - inicializacion (0x8002D2C0: SpuInit, borrado de la RAM de sonido);
       - carga del banco comun (0x8002D544): la RAM de sonido debe contener sus datos;
       - para cada sonido del banco: peticion (0x8002D724) y un fotograma del driver
         (0x8002DDCC); la voz debe arrancar con la direccion y el ADSR del banco y con un
         tono que de la frecuencia que calcula acesnd (ver alli la formula);
       - fotogramas hasta que el driver libere la voz, con la SPU generando audio: el
         sonido debe terminar (SpuGetAllKeysStatus a traves de ENVX) y sonar.

Uso:
    python3 tools/psxcheck_snd.py data/exe/ace_eu.bin [salida.wav]
"""
import ctypes
import glob
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acerec import Record  # noqa: E402
from acesnd import SoundBank, decode_adpcm  # noqa: E402
from mipsemu import Machine  # noqa: E402
from psyqcheck_gpu import build  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# libspu del ACE.BIN europeo -> funcion de psx/ (con los nombres reales: ver psx_libspu.h)
LIB = {0x8009E218: ('SpuInit', ''), 0x8009D630: ('SpuStart', ''), 0x8009E238: ('SpuQuit', ''),
       0x8009F888: ('SpuSetCommonAttr', 'p'), 0x8009FD68: ('SpuSetVoiceAttr', 'p'), 0x8009F3C8: ('SpuSetKey', 'ii'),
       0x8009FCDC: ('SpuGetAllKeysStatus', 'p'), 0x8009F7A8: ('SpuSetTransferMode', 'i'),
       0x8009F748: ('SpuSetTransferStartAddr', 'i'), 0x8009F588: ('SpuWrite', 'pi'), 0x8009F5E8: ('SpuWrite0', 'i'),
       0x8009F7D8: ('SpuIsTransferCompleted', 'i'), 0x8009E2B8: ('SpuSetReverb', 'i'),
       0x8009EE48: ('SpuSetReverbDepth', 'p'), 0x8009EEC8: ('SpuSetReverbVoice', 'ii'),
       0x8009E498: ('SpuSetReverbModeParam', 'p'), 0x8009F1B8: ('SpuClearReverbWorkArea', 'i')}
SND_INIT, BANK_LOAD, PLAY, DRIVER = 0x8002D2C0, 0x8002D544, 0x8002D724, 0x8002DDCC
G_STATE_PTR, G_STATE = 0x800BACD0, 0x800BD158
BANK_RAM = 0x80150000
FRAME = 44100 // 25        # muestras por fotograma del juego (25 Hz en PAL)


def vol_reg(v):
    return struct.unpack('<h', struct.pack('<H', (v << 1) & 0xFFFF))[0]


def check_decode(lib, bank):
    spu = ctypes.c_void_p(lib.psx_spu())
    w, rd = lib.psx_spu_write16, lib.psx_spu_read16
    lib.SpuInit()
    lib.SpuSetTransferStartAddr(bank.spu_base)
    lib.SpuWrite(bank.data, len(bank.data))
    bad = 0
    out = (ctypes.c_int16 * 2)()
    for k, (s, e) in enumerate(bank.samples()):
        ref, _ = decode_adpcm(bank.data, s, e)
        w(spu, 0x18C, 1)                       # KOFF y silencio
        w(spu, 0x180, 0x3FFF); w(spu, 0x182, 0x3FFF)
        w(spu, 0, 0x3FFF); w(spu, 2, 0x3FFF); w(spu, 4, 0x1000)
        w(spu, 6, (bank.spu_base + s) >> 3)
        w(spu, 8, 0x000F)                      # ataque lineal 0 (inmediato), caida 0, SL 15
        w(spu, 10, 0x1FC0)                     # sostenido 0x7F creciente (constante), RR 0
        w(spu, 0x188, 1)                       # KON
        got = []
        for _ in range(len(ref) + 1):
            lib.psx_spu_render(spu, out, 1)
            got.append(out[0])
        last = bank.data[e - 16 + 1]
        v = 0x7FFE
        exp = [max(-32768, min(32767, ((max(-32768, min(32767, (x * 0x7FFF >> 15) * v >> 15))) * v) >> 15))
               for x in ref]
        n_ok = sum(1 for i in range(4, len(ref) - 1) if got[i + 1] == exp[i])
        stopped = (rd(spu, 12) == 0) if not last & 2 else True
        endx = rd(spu, 0x19C) & 1
        ok = n_ok == len(ref) - 5 and stopped and endx
        bad += not ok
        if not ok and bad <= 3:
            print('  muestra %d: %d/%d iguales, parada %s, ENDX %d' % (k, n_ok, len(ref) - 5, stopped, endx))
    n = len(bank.samples())
    print('SPU frente a acesnd: %d/%d muestras del banco comun identicas (con fin de muestra y ENDX)' % (n - bad, n))
    return bad


class Driver:
    def __init__(self, lib, acebin):
        self.lib = lib
        self.ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
        self.base = ctypes.addressof(self.ram)
        self.m = m = Machine.from_acebin(acebin, ram=self.ram)
        for addr, (name, sig) in LIB.items():
            f = getattr(lib, name)
            f.restype = ctypes.c_int32

            def h(mm, f=f, sig=sig):
                a = [ctypes.c_void_p(self.base + (mm.r[4 + k] & 0x1FFFFF)) if t == 'p' else
                     ctypes.c_int32(struct.unpack('<i', struct.pack('<I', mm.r[4 + k]))[0]) for k, t in enumerate(sig)]
                return f(*a) or 0
            m.hooks[addr] = h
        m.sw(G_STATE_PTR, G_STATE)
        m.sb(G_STATE + 0x39C, 0xFF)    # volumen de efectos
        m.sb(G_STATE + 0x39D, 0x7F)
        m.sb(G_STATE + 0x39E, 0)       # estereo


def check_driver(lib, acebin, bank, raw, wav):
    d = Driver(lib, acebin)
    m = d.m
    spu = ctypes.c_void_p(lib.psx_spu())
    rd = lib.psx_spu_read16
    m.call(SND_INIT, [])
    m.ram[BANK_RAM & 0x1FFFFF:(BANK_RAM & 0x1FFFFF) + len(raw)] = raw
    m.call(BANK_LOAD, [BANK_RAM])
    spuram = ctypes.string_at(lib.psx_spu() + 0x400, 512 * 1024)   # PsxSpu.ram tras los registros
    loaded = spuram[bank.spu_base:bank.spu_base + bank.size] == bank.data
    print('driver: banco comun cargado por el codigo del juego: %s' % ('RAM de sonido identica' if loaded else 'DISTINTA'))
    bad = 0 if loaded else 1
    out = (ctypes.c_int16 * (2 * FRAME))()
    pcm = bytearray()
    tested = 0
    for i, e in enumerate(bank.entries):
        if e['adsr'] == 0 or tested >= 40:
            continue
        slot = bank.b0 + i
        voice = m.call(PLAY, [slot]) & 0xFF
        if voice == 0xFF:
            continue
        m.call(DRIVER, [])
        b = voice * 16
        pitch, addr = rd(spu, b + 4), rd(spu, b + 6) * 8
        adsr = rd(spu, b + 8) | rd(spu, b + 10) << 16
        f_lib = pitch * 44100 / 4096
        # frecuencia segun acesnd (corregida: la fraccion u0 SUMA, en 1/128 de semitono)
        f_ref = 44100 * 2 ** (((e['vol'] >> 8) - e['note'] + ((e['vol'] & 0xFF) + e['u0']) / 128) / 12)
        ok = addr == bank.spu_base + e['off'] and adsr == e['adsr'] and abs(f_lib / f_ref - 1) < 0.002
        frames = 0
        loud = 0
        while frames < 400:
            lib.psx_spu_render(spu, out, FRAME)
            loud = max(loud, max(abs(x) for x in out))
            if len(pcm) < 44100 * 2 * 2 * 20:
                pcm += bytes(out)
            m.call(DRIVER, [])
            frames += 1
            if rd(spu, b + 12) == 0 and m.lb(0x800C35F8 + 0x14 * voice + 3) == 0:
                break
        loops = frames >= 400
        ok = ok and loud > 0 and (frames < 400 or loops)
        bad += not ok
        tested += 1
        if not ok or tested <= 3:
            print('  sonido %2d -> voz %2d: dir %05x adsr %08x tono %04x (%.0f Hz, acesnd %.0f Hz), %s, pico %d: %s'
                  % (slot, voice, addr, adsr, pitch, f_lib, f_ref,
                     'en bucle' if loops else 'termina en %d fotogramas' % frames, loud, 'bien' if ok else 'MAL'))
        if loops:   # sonidos en bucle (motores...): se paran con KOFF para seguir
            lib.SpuSetKey(0, 1 << voice)
            for _ in range(60):
                lib.psx_spu_render(spu, out, FRAME)
                m.call(DRIVER, [])
    print('driver: %d sonidos del banco comun tocados por el codigo del juego, %d fallos' % (tested, bad))
    if wav:
        with wave.open(wav, 'wb') as f:
            f.setnchannels(2); f.setsampwidth(2); f.setframerate(44100)
            f.writeframes(bytes(pcm))
        print('audio: %s' % wav)
    return bad


def check_capture(lib):
    """captura de la SPU (SpuReadDecodedData): la entrada de CD aparece en cd_left/cd_right"""
    spu = ctypes.c_void_p(lib.psx_spu())
    lib.SpuInit()
    w = lib.psx_spu_write16
    w(spu, 0x1AA, 0xC001)                       # SPU encendida y entrada de CD activa
    w(spu, 0x1B0, 0x7FFF); w(spu, 0x1B2, 0x7FFF)
    n = 0x180
    st = (ctypes.c_int16 * (2 * n))(*[v for i in range(n) for v in (i * 37 - 3000, -i * 11)])
    lib.psx_spu_cd_push(spu, st, n)
    out = (ctypes.c_int16 * (2 * n))()
    lib.psx_spu_render(spu, out, n)
    d = ctypes.create_string_buffer(0x1000)
    half = lib.SpuReadDecodedData(d, 0)
    cdl = struct.unpack_from('<512h', d.raw, 0)
    cdr = struct.unpack_from('<512h', d.raw, 0x400)
    want_l = [(i * 37 - 3000) * 0x7FFF >> 15 for i in range(n)]
    want_r = [(-i * 11) * 0x7FFF >> 15 for i in range(n)]
    ok = list(cdl[:n]) == want_l and list(cdr[:n]) == want_r and half == 1
    print('captura de la SPU (SpuReadDecodedData): %s' % ('bien' if ok else 'MAL'))
    return 0 if ok else 1


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    lib = build()
    lib.psx_spu.restype = ctypes.c_size_t
    lib.psx_spu_read16.restype = ctypes.c_uint16
    raw = Record.from_file(glob.glob(os.path.join(ROOT, 'data', 'bpb', '0000_*'))[0]).section(3)
    bank = SoundBank(raw)
    fails = check_decode(lib, bank)
    fails += check_capture(lib)
    fails += check_driver(lib, sys.argv[1], bank, raw, sys.argv[2] if len(sys.argv) > 2 else None)
    print('TODO BIEN' if not fails else 'HAY FALLOS (%d)' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
