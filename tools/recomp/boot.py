#!/usr/bin/env python3
"""boot — arranca Ace Combat 3 en el interprete (codigo ORIGINAL de ACE.BIN) sobre la capa
PsyQ de psx/ (etapa 6). Es la ejecucion de referencia: el mismo arranque se usara para
comparar con el codigo recompilado (tools/recomp/record.py) y como guia del paso 4.

Como ac3_start (0x8002521C): BSS a cero, InitHeap y ac3_main (0x80025D08), que no vuelve.
Entorno: imagen de disco en libcd, mando digital en el puerto 1 (sin botones salvo los que
se programen), tarjeta de memoria nueva en una imagen temporal, tablas de la GTE leidas
del ACE.BIN, callbacks de la capa devueltos al interprete. La GTE del interprete es la
misma que usan las funciones de libgte (el juego mezcla las dos cosas).
Se para tras N fotogramas (llamadas a VSync que avanzan el reloj) y guarda la pantalla.

Guion del mando: "fotograma:boton[+boton]:duracion,..." (botones: cruz, circulo, start,
select, arriba, abajo, izquierda, derecha, triangulo, cuadrado, l1, r1, l2, r2).

Uso:
    python3 tools/recomp/boot.py <imagen.bin> [fotogramas] [dir_capturas] [cada_n] [guion]
"""
import ctypes
import glob
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import hle  # noqa: E402
from mipsemu import Machine, PsxGteLib  # noqa: E402
from tim2png import write_png  # noqa: E402

AC3_MAIN = 0x80025D08
# instrucciones por fotograma PAL: 33,8688 MHz / 50 = 677.376 ciclos; con unos 1,5 ciclos por
# instruccion (esperas de memoria), ~450.000 instrucciones. Aproximado (SIN VERIFICAR).
INSNS_PER_FRAME = 450_000
BUTTONS = {'select': 1 << 0, 'start': 1 << 3, 'arriba': 1 << 4, 'derecha': 1 << 5, 'abajo': 1 << 6,
           'izquierda': 1 << 7, 'l2': 1 << 8, 'r2': 1 << 9, 'l1': 1 << 10, 'r1': 1 << 11, 'triangulo': 1 << 12,
           'circulo': 1 << 13, 'cruz': 1 << 14, 'cuadrado': 1 << 15}


def parse_script(text):
    out = []
    for item in filter(None, (text or '').split(',')):
        f, btn, dur = item.split(':')
        mask = 0
        for b in btn.split('+'):
            mask |= BUTTONS[b]
        out.append((int(f), mask, int(dur)))
    return out
CALL_T = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)


class Stop(Exception):
    pass


class GlobalGte(PsxGteLib):
    """la GTE de psx/ que usan las funciones de libgte (contexto global)"""
    def __init__(self, lib):
        super().__init__(lib)
        lib.psx_gte_get_context.restype = ctypes.c_void_p
        self.ctx = ctypes.c_void_p(lib.psx_gte_get_context())

    def state(self):
        return ctypes.string_at(self.ctx.value, 256)

    def set_state(self, raw):
        ctypes.memmove(self.ctx.value, raw, 256)


class Boot:
    def __init__(self, image, libpath=None, acebin=None):
        libpath = libpath or os.path.join(ROOT, 'data', 'recomp', 'librecomp.so')
        acebin = acebin or os.path.join(ROOT, 'data', 'exe', 'ace_eu.bin')
        self.lib = lib = ctypes.CDLL(libpath)
        self.ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
        self.scr = (ctypes.c_uint8 * 0x400).in_dll(lib, 'psx_scratch')
        self.gte = GlobalGte(lib)
        self.m = Machine.from_acebin(acebin, ram=self.ram, scratch=self.scr, gte=self.gte)
        ace = open(acebin, 'rb').read()
        lib.psx_gte_tables_init(ace, ctypes.c_size_t(len(ace)))
        if lib.psx_cd_open(image.encode()) != 0:
            raise SystemExit('no se abre la imagen')
        lib.psx_pad_connect(0, 0x41)
        lib.psx_pad_connect(1, 0)
        self.card = os.path.join(tempfile.mkdtemp(), 'tarjeta1.mcd')
        lib.psx_card_attach(0, self.card.encode())
        lib.psx_card_detach(1)
        lib.psx_set_refresh(50)
        self.cb = CALL_T(self.dispatch)
        lib.psx_set_call(self.cb)
        self.hle = hle.Hle(lib, self.m, self.ram, self.scr)
        self.hle.install(hle.lib_targets(acebin, sorted(glob.glob(os.path.join(ROOT, 'data', 'exe', 'ovl', '*.exe')))))
        if self.hle.missing:   # se ejecutaria el codigo original de la libreria (y la BIOS)
            raise SystemExit('funciones de libreria sin implementacion en %s: %s' % (libpath, sorted(self.hle.missing)))
        self.frames = 0
        self.max_frames = None
        self.on_frame = None
        self.script = []
        vs = [a for a, n in hle.lib_targets(acebin).items() if n == 'VSync'][0]
        inner = self.m.hooks[vs]

        def frame_done(before, after):
            if after == before:
                return
            self.frames += after - before
            mask = 0
            for f0, msk, dur in self.script:
                if f0 <= self.frames < f0 + dur:
                    mask |= msk
            lib.psx_pad_set(0, mask, 0x80, 0x80, 0x80, 0x80)
            if self.on_frame:
                self.on_frame(self)
            if self.max_frames is not None and self.frames >= self.max_frames:
                raise Stop()

        def vsync(mm):
            before = lib.VSync(-1)
            v = inner(mm)
            frame_done(before, lib.VSync(-1))
            mm.next_tick = mm.total_steps + mm.tick_every   # VSync espero al VBlank: el reloj empieza de nuevo
            return v
        self.m.hooks[vs] = vsync

        def tick(mm):   # el tiempo pasa aunque el juego no llame a VSync (bucles de espera)
            before = lib.VSync(-1)
            lib.psx_vblank()
            frame_done(before, lib.VSync(-1))
        self.m.on_tick = tick
        self.m.tick_every = INSNS_PER_FRAME

    def dispatch(self, addr, a0, a1):
        self.m.call(addr, [a0, a1], max_steps=50_000_000)

    def screen(self, path):
        out = (ctypes.c_uint32 * (640 * 512))()
        w, h = ctypes.c_int(), ctypes.c_int()
        self.lib.psx_display_frame(out, 640 * 512, ctypes.byref(w), ctypes.byref(h))
        if w.value and h.value:
            write_png(path, w.value, h.value, ctypes.string_at(ctypes.addressof(out), w.value * h.value * 4))
        return w.value, h.value

    def run(self, frames):
        m = self.m
        for a in range(0x800BACB0, 0x800C9DC0, 4):   # BSS, como ac3_start
            m.sw(a, 0)
        self.max_frames = frames
        t = time.time()
        try:
            m.call(AC3_MAIN, [], max_steps=10**12)
        except Stop:
            pass
        return time.time() - t


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    image = sys.argv[1]
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 100
    shots = sys.argv[3] if len(sys.argv) > 3 else None
    every = int(sys.argv[4]) if len(sys.argv) > 4 else 25
    b = Boot(image)
    b.script = parse_script(sys.argv[5] if len(sys.argv) > 5 else '')
    steps = [0]

    def frame(bb):
        if shots and bb.frames % every == 0:
            os.makedirs(shots, exist_ok=True)
            w, h = bb.screen(os.path.join(shots, 'f%05d.png' % bb.frames))
            print('fotograma %d: pantalla %dx%d, %d llamadas a librerias' % (bb.frames, w, h, bb.hle.calls), flush=True)
    b.on_frame = frame
    dt = b.run(frames)
    print('%d fotogramas en %.1f s (%.2f fotogramas/s del juego en el interprete)' % (b.frames, dt, b.frames / dt))
    return 0


if __name__ == '__main__':
    sys.exit(main())
