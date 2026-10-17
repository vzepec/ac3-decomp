#!/usr/bin/env python3
"""record — valida el codigo recompilado con ESTADOS REALES del juego (etapa 6).

Durante el arranque real en el interprete (tools/recomp/boot.py: codigo original de
ACE.BIN sobre la capa psx/), al entrar en una funcion elegida se toma una instantanea
(registros, HI/LO, RAM, scratchpad, GTE, overlay cargado). Cada llamada a libreria dentro
de ella se graba: direccion, a0-a3, valor devuelto y los cambios que hace en la RAM, el
scratchpad y la GTE (incluidos los de los callbacks que la libreria entrega). Al volver la
funcion, en el momento, la funcion RECOMPILADA (data/recomp/librecomp.so) se ejecuta desde
la instantanea con las llamadas a libreria REPRODUCIDAS (comprobando direccion y
argumentos, aplicando sus efectos) y se compara el estado final con el del interprete:
registros, HI/LO, RAM, scratchpad y GTE. Luego se restaura la RAM viva y el arranque sigue.
Se descartan los casos en que salta una interrupcion del reloj en mitad de la funcion (no
se puede reproducir en el mismo punto) o que no vuelven en un numero razonable de pasos.

Uso:
    python3 tools/recomp/record.py <imagen.bin> [fotogramas] [muestras_por_funcion] [guion]
Escribe data/recomp/record.json.
"""
import ctypes
import glob
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import boot  # noqa: E402
from check import RCpu, LIBCALL, STATUS  # noqa: E402

OVL_BASE = 0x800C9DC0
MAX_STEPS = 3_000_000
MAX_CALLS = 400


def diff(a, b):
    """[(desplazamiento, bytes nuevos)] entre dos copias de la memoria, por paginas de 4 KB"""
    out = []
    for p in range(0, len(a), 4096):
        x, y = a[p:p + 4096], b[p:p + 4096]
        if x != y:
            i = 0
            while i < len(x):
                if x[i] != y[i]:
                    j = i
                    while j < len(x) and x[j] != y[j]:
                        j += 1
                    out.append((p + i, y[i:j]))
                    i = j
                else:
                    i += 1
    return out


class Recorder:
    def __init__(self, image, samples):
        self.b = b = boot.Boot(image)
        self.lib = lib = b.lib
        self.samples = samples
        self.ovl_files = {k: open(os.path.join(ROOT, 'data', 'exe', 'ovl', 'overlay%d.exe' % k), 'rb').read()[2048:2048 + 256]
                          for k in range(1, 6)}
        # tablas de funciones recompiladas (residente y overlays)
        meta = {'ace_eu': json.load(open(os.path.join(ROOT, 'data', 'recomp', 'ace_eu.json')))}
        for k in range(1, 6):
            meta['overlay%d' % k] = json.load(open(os.path.join(ROOT, 'data', 'recomp', 'overlay%d.json' % k)))
        self.names = {}
        lib.rc_register.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        t = (ctypes.c_uint8 * 8).in_dll(lib, 'rc_funcs_ace_eu')
        lib.rc_register(ctypes.addressof(t), ctypes.c_uint.in_dll(lib, 'rc_nfuncs_ace_eu').value)
        lib.rc_register_overlay.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint]
        for k in range(1, 6):
            t = (ctypes.c_uint8 * 8).in_dll(lib, 'rc_funcs_overlay%d' % k)
            lib.rc_register_overlay(k, ctypes.addressof(t), ctypes.c_uint.in_dll(lib, 'rc_nfuncs_overlay%d' % k).value)
        self.res_entries = {m['addr'] for m in meta['ace_eu']}
        self.ovl_entries = {k: {m['addr'] for m in meta['overlay%d' % k]} for k in range(1, 6)}
        for key, ms in meta.items():
            for m in ms:
                self.names[(key, m['addr'])] = m['name']
        self.rec = None
        self.results = {}       # (modulo, addr) -> [veredictos]
        self.gte_b = ctypes.create_string_buffer(256)
        ctypes.c_void_p.in_dll(lib, 'rc_gte').value = ctypes.addressof(self.gte_b)
        lib.rc_run.restype = ctypes.c_int
        lib.rc_run.argtypes = [ctypes.POINTER(RCpu), ctypes.c_uint32, ctypes.c_int64,
                               ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
        self.cb = LIBCALL(self.replay_libcall)
        lib.rc_set_libcall(self.cb)
        for a in self.res_entries | set().union(*self.ovl_entries.values()):
            b.m.probes[a] = None   # install_enter pone la funcion de cada entrada
        b.hle.on_call = self.on_call
        self.ticks_in_rec = 0
        inner_tick = b.m.on_tick

        def tick(mm):
            if self.rec:   # interrupcion en mitad de la funcion: no se puede reproducir
                self.cancel(mm)
            inner_tick(mm)
        b.m.on_tick = tick
        self.stats = {'iguales': 0, 'distintos': 0, 'descartados': 0}

    def current_overlay(self):
        cur = bytes(self.b.ram[OVL_BASE & 0x1FFFFF:(OVL_BASE & 0x1FFFFF) + 256])
        for k, d in self.ovl_files.items():
            if cur == d:
                return k
        return 0

    def module(self, addr):
        if addr >= OVL_BASE:
            k = self.current_overlay()
            return ('overlay%d' % k, k) if k and addr in self.ovl_entries[k] else (None, k)
        return ('ace_eu', self.current_overlay())

    # ---- grabacion ----
    def cancel(self, mm):
        r = self.rec
        self.rec = None
        if r['prev_probe']:
            mm.probes[r['ra']] = r['prev_probe']
        else:
            mm.probes.pop(r['ra'], None)
        self.stats['descartados'] += 1

    def on_call(self, addr, name, mm, before, v=None):
        r = self.rec
        if r is None:
            return
        if before:
            if len(r['calls']) >= MAX_CALLS or mm.total_steps - r['steps'] > MAX_STEPS:
                self.cancel(mm)
                return
            r['pending'] = (addr, [mm.r[4], mm.r[5], mm.r[6], mm.r[7]], bytes(self.b.ram), bytes(self.b.scr))
        else:
            if 'pending' not in r:
                return
            addr0, a, ram0, scr0 = r.pop('pending')
            r['calls'].append((addr0, a, v & 0xFFFFFFFF, mm.r[3] if name in ('__divdi3', '__udivdi3') else None,
                               diff(ram0, bytes(self.b.ram)), diff(scr0, bytes(self.b.scr)), self.b.gte.state()))

    def start(self, mm, addr):
        # solo si se llego LLAMANDO: la instruccion anterior es el hueco de retardo de un jal o
        # jalr (una entrada que es un final compartido o una etiqueta interna tambien se
        # alcanza por saltos dentro de la funcion)
        p = mm.prev_pc
        try:
            w = mm.lw((p - 4) & 0xFFFFFFFF)
        except Exception:
            return
        called = (w >> 26 == 3) or (w >> 26 == 0 and (w & 63) == 9)
        if not called or mm.r[31] != ((p + 4) & 0xFFFFFFFF):
            return
        mod, k = self.module(addr)
        if mod is None:
            return
        key = (mod, addr)
        done = self.results.get(key, [])
        if len(done) >= self.samples:
            return
        self.rec = {'key': key, 'addr': addr, 'ovl': k, 'regs': list(mm.r), 'hi': mm.hi, 'lo': mm.lo,
                    'ram': bytes(self.b.ram), 'scr': bytes(self.b.scr), 'gte': self.b.gte.state(),
                    'ra': mm.r[31], 'sp': mm.r[29], 'depth': mm.depth, 'steps': mm.total_steps,
                    'calls': [], 'tick': False, 'abort': False}
        prev = mm.probes.get(mm.r[31])
        self.rec['prev_probe'] = prev
        mm.probes[mm.r[31]] = self.leave

    def leave(self, mm):
        r = self.rec
        if r is None:
            return
        if mm.r[29] != r['sp'] or mm.depth != r['depth']:
            if r['prev_probe']:
                r['prev_probe'](mm)
            return
        ra = r['ra']
        if r['prev_probe']:
            mm.probes[ra] = r['prev_probe']
        else:
            mm.probes.pop(ra, None)
        self.rec = None
        if r['tick'] or r['abort'] or 'pending' in r:
            self.stats['descartados'] += 1
        else:
            self.replay(r, mm)
        if r['prev_probe']:
            r['prev_probe'](mm)

    # ---- reproduccion ----
    def replay_libcall(self, cpu, addr):
        c = cpu.contents
        st = self.rp
        if st['i'] >= len(st['calls']):
            st['err'] = st['err'] or 'llamada de mas a %08x' % addr
            return
        addr0, a, v0, v1, dram, dscr, gte = st['calls'][st['i']]
        st['i'] += 1
        got = [c.r[4], c.r[5], c.r[6], c.r[7]]
        if addr0 != addr or got != a:
            st['err'] = st['err'] or 'llamada %d: %08x(%s) / grabada %08x(%s)' % (
                st['i'], addr, ','.join('%x' % x for x in got), addr0, ','.join('%x' % x for x in a))
        base, sbase = ctypes.addressof(self.b.ram), ctypes.addressof(self.b.scr)
        for off, data in dram:
            ctypes.memmove(base + off, data, len(data))
        for off, data in dscr:
            ctypes.memmove(sbase + off, data, len(data))
        ctypes.memmove(self.gte_b, gte, 256)
        c.r[2] = v0
        if v1 is not None:
            c.r[3] = v1

    def replay(self, r, mm):
        lib, b = self.lib, self.b
        end = (list(mm.r), mm.hi, mm.lo, bytes(b.ram), bytes(b.scr), b.gte.state())
        base = ctypes.addressof(b.ram)
        ctypes.memmove(base, r['ram'], 0x200000)
        ctypes.memmove(ctypes.addressof(b.scr), r['scr'], 0x400)
        ctypes.memmove(self.gte_b, r['gte'], 256)
        lib.rc_set_overlay(r['ovl'])
        cpu = RCpu()
        for i in range(32):
            cpu.r[i] = r['regs'][i]
        cpu.hi, cpu.lo = r['hi'], r['lo']
        self.rp = {'calls': r['calls'], 'i': 0, 'err': None}
        fpc, fad = ctypes.c_uint32(), ctypes.c_uint32()
        st = lib.rc_run(ctypes.byref(cpu), r['addr'], 10**8, ctypes.byref(fpc), ctypes.byref(fad))
        got = (list(cpu.r), cpu.hi, cpu.lo, bytes(b.ram), bytes(b.scr), bytes(self.gte_b.raw))
        verdict = None
        if st != 0:
            verdict = 'fallo %s en %08x (dir %08x)' % (STATUS.get(st, st), fpc.value, fad.value)
        elif self.rp['err']:
            verdict = self.rp['err']
        elif self.rp['i'] != len(r['calls']):
            verdict = '%d llamadas a libreria / grabadas %d' % (self.rp['i'], len(r['calls']))
        else:
            for name, x, y in zip(('registros', 'hi', 'lo', 'ram', 'scratch', 'gte'), end, got):
                if x != y:
                    if name == 'registros':
                        verdict = 'registros ' + ','.join('r%d %08x/%08x' % (i, x[i], y[i]) for i in range(32) if x[i] != y[i])[:100]
                    elif name == 'ram':
                        i = next(i for i in range(len(x)) if x[i] != y[i])
                        verdict = 'ram: primer byte distinto en %08x' % (0x80000000 | i)
                    else:
                        verdict = name
                    break
        ctypes.memmove(base, end[3], 0x200000)   # RAM viva
        ctypes.memmove(ctypes.addressof(b.scr), end[4], 0x400)
        self.results.setdefault(r['key'], []).append(verdict or 'igual')
        self.stats['iguales' if verdict is None else 'distintos'] += 1
        if verdict and sum(1 for v in self.results.values() if any(x != 'igual' for x in v)) <= 30:
            print('  DISTINTA %s %08x %-22s %s' % (r['key'][0], r['addr'], self.names.get(r['key'], ''), verdict), flush=True)


def install_enter(rec):
    """las sondas necesitan saber la direccion: una funcion por entrada"""
    m = rec.b.m
    for a in list(m.probes):
        def enter(mm, a=a):
            if rec.rec is None and not mm.in_tick:
                rec.start(mm, a)
        m.probes[a] = enter


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    image = sys.argv[1]
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 600
    samples = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    rec = Recorder(image, samples)
    install_enter(rec)
    rec.b.script = boot.parse_script(sys.argv[4] if len(sys.argv) > 4 else '320:cruz:6')

    shots = os.path.join(ROOT, 'data', 'recomp', 'shots')
    os.makedirs(shots, exist_ok=True)

    def frame(bb):
        if bb.frames % 100 == 0:
            bb.screen(os.path.join(shots, 'f%05d.png' % bb.frames))
            print('fotograma %d: %d funciones distintas probadas; %s' % (bb.frames, len(rec.results), rec.stats), flush=True)
    rec.b.on_frame = frame
    t = time.time()
    rec.b.run(frames)
    ok = [k for k, v in rec.results.items() if all(x == 'igual' for x in v)]
    bad = {k: v for k, v in rec.results.items() if any(x != 'igual' for x in v)}
    print('%d fotogramas en %.0f s; %d funciones probadas con estados reales: %d iguales, %d distintas; %s'
          % (rec.b.frames, time.time() - t, len(rec.results), len(ok), len(bad), rec.stats))
    json.dump({'%s:%08x' % k: v for k, v in sorted(rec.results.items())},
              open(os.path.join(ROOT, 'data', 'recomp', 'record.json'), 'w'), indent=1)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
