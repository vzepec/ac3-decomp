#!/usr/bin/env python3
"""check — valida el codigo recompilado funcion a funcion contra el interprete.

Para cada funcion traducida (data/recomp/<binario>.json) y varios estados iniciales
aleatorios pero plausibles (argumentos: enteros pequenos, valores de 16 bits y punteros a
una zona de datos aleatorios o a las estructuras del juego; $sp, $gp y $ra validos; GTE
con registros aleatorios), se ejecuta:
  A) el codigo ORIGINAL en tools/mipsemu.py;
  B) la funcion recompilada (data/recomp/librecomp.so, rc_run);
desde la MISMA RAM (ACE.BIN cargado + zona aleatoria), y se comparan: estado de salida
(normal o fallo y su tipo), los 32 registros, HI/LO, los 2 MB de RAM, el scratchpad, los
64 registros de la GTE y la SECUENCIA de llamadas a librerias de Sony. Las llamadas a
librerias van, en los dos lados, al mismo stub determinista (registra la direccion y
a0-a3 y devuelve en v0 un valor que depende de ellos): asi se prueba la traduccion del
codigo del juego, no las librerias (validadas en la etapa 5 y el paso 1).
Veredicto por funcion: VALIDADA (al menos un caso que termina normalmente, identico, y
ningun caso distinto), DISTINTA (algun caso difiere) o SIN CASO (solo fallos o tiempo
agotado en los dos lados). Los casos en que alguno agota el tiempo no cuentan.

Uso:
    python3 tools/recomp/check.py data/exe/ace_eu.bin data/recomp [casos] [procesos] [funcion...]
    python3 tools/recomp/check.py data/exe/ace_eu.bin data/recomp --overlay 1 data/exe/ovl/overlay1.exe [casos]
Otra version (US): --region data/recomp_us/region.json (tools/recomp/region.py) pone sus
direcciones (libreria, codigo propio, overlays, $gp, estado del juego).
Escribe data/recomp/<binario>_check.json y actualiza docs/progreso.json (recompiladas; solo la
version europea, la que cuenta la infografia).
"""
import ctypes
import json
import concurrent.futures as cf
import os
import random
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mipsemu import Machine, PsxGteLib, RETURN_MAGIC, GP_EU  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LIB_LO, LIB_HI = 0x80099DE0, 0x800AE09C
ARENA = 0x80180000          # zona de datos aleatorios para los punteros de los casos
ARENA_SIZE = 0x40000
G_STATE_PTR, G_STATE = 0x800BACD0, 0x800BD158
OWN_LO, OWN_HI, LIB_MAIN, OVL_LO, OVL_HI, GP = 0x800252CC, 0x80099DE0, 0x80025214, 0x800C9DC0, 0x800F9E18, GP_EU
REGION = None


def apply_region(path):
    """direcciones de otra version (perfil de tools/recomp/region.py)"""
    global LIB_LO, LIB_HI, G_STATE_PTR, G_STATE, OWN_LO, OWN_HI, LIB_MAIN, OVL_LO, OVL_HI, GP, REGION
    p = REGION = json.load(open(path))
    LIB_LO, LIB_HI = p['lib']
    G_STATE_PTR, G_STATE = p['g_state_ptr'], p['g_state']
    OWN_LO, OWN_HI, LIB_MAIN = p['own_start'], p['lib'][0], p['extra_lib'][0]
    OVL_LO, OVL_HI, GP = p['ovl_base'], max(p['ovl_end'].values()), p['gp']


STATUS = {0: 'ok', 1: 'mem', 2: 'align', 3: 'break', 4: 'syscall', 5: 'ilegal', 6: 'timeout', 7: 'nofunc', 8: 'io', 9: 'badra'}


class RCpu(ctypes.Structure):
    _fields_ = [('r', ctypes.c_uint32 * 32), ('hi', ctypes.c_uint32), ('lo', ctypes.c_uint32)]


LIBCALL = ctypes.CFUNCTYPE(None, ctypes.POINTER(RCpu), ctypes.c_uint32)


def stub_value(addr, a):
    h = addr
    for v in a:
        h = (h * 0x9E3779B1 + v) & 0xFFFFFFFF
    return (h >> 13) & 3


class World:
    def __init__(self, acebin, out_dir, ovl=None, region=None):
        if region:
            apply_region(region)
        stem = os.path.splitext(os.path.basename(acebin))[0]
        self.lib = lib = ctypes.CDLL(os.path.join(out_dir, 'librecomp.so'))
        self.ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
        self.scr = (ctypes.c_uint8 * 0x400).in_dll(lib, 'psx_scratch')
        funcs = (ctypes.c_uint8 * 8).in_dll(lib, 'rc_funcs_' + stem)
        n = ctypes.c_uint.in_dll(lib, 'rc_nfuncs_' + stem).value
        lib.rc_register.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        lib.rc_register(ctypes.addressof(funcs), n)
        self.log = []
        self.cb = LIBCALL(self.libcall_c)
        lib.rc_set_libcall(self.cb)
        lib.rc_set_region.argtypes = [ctypes.c_uint32] * 5
        lib.rc_set_region(OVL_LO, LIB_LO, LIB_HI, LIB_MAIN, GP)
        self.gte_a = PsxGteLib(lib)                       # interprete
        self.gte_b = ctypes.create_string_buffer(256)     # recompilado
        ctypes.c_void_p.in_dll(lib, 'rc_gte').value = ctypes.addressof(self.gte_b)
        self.m = Machine.from_acebin(acebin, gp=GP, ram=self.ram, scratch=self.scr, gte=self.gte_a)
        self.m.sw(G_STATE_PTR, G_STATE)
        if ovl:   # overlay cargado en su base (EU 0x800C9DC0), como lo hace el juego
            n, path = ovl
            d = open(path, 'rb').read()
            addr, size = struct.unpack_from('<II', d, 0x18)
            ctypes.memmove(ctypes.addressof(self.ram) + (addr & 0x1FFFFF), d[2048:2048 + size], size)
            ostem = os.path.splitext(os.path.basename(path))[0]
            ot = (ctypes.c_uint8 * 8).in_dll(lib, 'rc_funcs_' + ostem)
            on = ctypes.c_uint.in_dll(lib, 'rc_nfuncs_' + ostem).value
            lib.rc_register_overlay.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint]
            lib.rc_register_overlay(n, ctypes.addressof(ot), on)
            lib.rc_set_overlay(n)
        self.base = bytes(self.ram)
        # libreria: todas las funciones de la zona de librerias a las que puede saltar el juego
        meta = json.load(open(os.path.join(out_dir, stem + '.json')))
        self.funcs = meta
        libaddrs = set()
        scan = list(range(OWN_LO, OWN_HI, 4))
        if ovl:
            scan += list(range(OVL_LO, OVL_HI, 4))
        for a in scan:
            w = self.m.lw(a)
            if w >> 26 == 3:
                t = ((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                if LIB_LO <= t < LIB_HI or t == LIB_MAIN:
                    libaddrs.add(t)
        for t in libaddrs:
            self.m.hooks[t] = self.libcall_py
        lib.rc_run.restype = ctypes.c_int
        lib.rc_run.argtypes = [ctypes.POINTER(RCpu), ctypes.c_uint32, ctypes.c_int64,
                               ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]

    def libcall_py(self, mm):
        a = [mm.r[4], mm.r[5], mm.r[6], mm.r[7]]
        self.log.append((mm.r[31] - 8 if False else 0, a[0], a[1], a[2], a[3]))
        return stub_value(0, a)

    def libcall_c(self, cpu, addr):
        c = cpu.contents
        a = [c.r[4], c.r[5], c.r[6], c.r[7]]
        self.log.append((0, a[0], a[1], a[2], a[3]))
        c.r[2] = stub_value(0, a)

    def case(self, rnd):
        """estado inicial: registros, HI/LO, zona aleatoria y GTE"""
        def val():
            k = rnd.randrange(10)
            if k < 3: return rnd.randrange(8)
            if k < 5: return rnd.getrandbits(16)
            if k < 7: return ARENA + (rnd.randrange(ARENA_SIZE - 0x2000) & ~3)
            if k < 8: return G_STATE + (rnd.randrange(0x400) & ~3)
            return rnd.getrandbits(32)
        regs = [val() for _ in range(32)]
        regs[0] = 0
        regs[28] = GP
        regs[29] = 0x801FF000
        regs[31] = RETURN_MAGIC
        # zona de datos: palabras que en su mayoria son punteros validos a la propia zona o
        # enteros pequenos, para que las funciones que recorren estructuras puedan terminar
        words = []
        for _ in range(1024):
            k = rnd.randrange(10)
            if k < 4: words.append(ARENA + (rnd.randrange(ARENA_SIZE - 0x2000) & ~3))
            elif k < 7: words.append(rnd.randrange(16))
            elif k < 9: words.append(rnd.getrandbits(16))
            else: words.append(rnd.getrandbits(32))
        arena = struct.pack('<1024I', *words) * (ARENA_SIZE // 4096)
        gte = bytes(rnd.getrandbits(8) for _ in range(256))
        return regs, rnd.getrandbits(32), rnd.getrandbits(32), arena, gte

    def reset(self, arena, gte):
        ctypes.memmove(self.ram, self.base, 0x200000)
        a = ARENA & 0x1FFFFF
        ctypes.memmove(ctypes.addressof(self.ram) + a, arena, len(arena))
        ctypes.memset(self.scr, 0, 0x400)
        self.gte_a.set_state(gte)
        ctypes.memmove(self.gte_b, gte, 256)

    def trial(self, addr, rnd, steps):
        regs, hi, lo, arena, gte = self.case(rnd)
        self.reset(arena, gte)
        self.log = []
        st_a, _ = self.m.run(addr, regs, hi, lo, max_steps=steps)
        res_a = (list(self.m.r), self.m.hi, self.m.lo, bytes(self.ram), bytes(self.scr), self.gte_a.state(), self.log)
        self.reset(arena, gte)
        self.log = []
        cpu = RCpu()
        for i in range(32):
            cpu.r[i] = regs[i]
        cpu.hi, cpu.lo = hi, lo
        fpc, fad = ctypes.c_uint32(), ctypes.c_uint32()
        st = self.lib.rc_run(ctypes.byref(cpu), addr, steps // 4, ctypes.byref(fpc), ctypes.byref(fad))
        st_b = STATUS.get(st, str(st))
        res_b = (list(cpu.r), cpu.hi, cpu.lo, bytes(self.ram), bytes(self.scr), bytes(self.gte_b.raw), self.log)
        if 'timeout' in (st_a, st_b):
            return 'timeout', None
        if st_a != 'ok' and st_b != 'ok':
            return 'fallo', st_a   # los dos fallan (p. ej. saltan a una direccion sin codigo): no se compara
        if st_a != st_b:
            return 'distinto', 'estado %s / %s (fallo en %08x, dir %08x)' % (st_a, st_b, fpc.value, fad.value)
        names = ['registros', 'hi', 'lo', 'ram', 'scratch', 'gte', 'librerias']
        for k, (x, y) in enumerate(zip(res_a, res_b)):
            if x != y:
                det = ''
                if k == 0:
                    det = ' ' + ','.join('r%d %08x/%08x' % (i, x[i], y[i]) for i in range(32) if x[i] != y[i])[:120]
                elif k == 3:
                    i = next(i for i in range(len(x)) if x[i] != y[i])
                    det = ' primer byte distinto en %08x' % (0x80000000 | i)
                elif k == 6:
                    det = ' %d / %d llamadas' % (len(x), len(y))
                return 'distinto', names[k] + det
        return 'igual', None


WORLD = None


def init(acebin, out_dir, ovl=None, region=None):
    global WORLD
    WORLD = World(acebin, out_dir, ovl, region)


def check_func(args):
    addr, ncases, steps = args
    rnd = random.Random(addr)
    counts = {'igual': 0, 'distinto': 0, 'fallo': 0, 'timeout': 0}
    first = None
    for _ in range(ncases):
        try:
            v, det = WORLD.trial(addr, rnd, steps)
        except Exception as e:   # error del arnes
            v, det = 'distinto', 'excepcion %s' % e
        counts[v] += 1
        if v == 'distinto' and first is None:
            first = det
    verdict = 'VALIDADA' if counts['igual'] and not counts['distinto'] else ('DISTINTA' if counts['distinto'] else 'SIN CASO')
    return addr, verdict, counts, first


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    args = sys.argv[1:]
    ovl, region = None, None
    if '--region' in args:
        i = args.index('--region')
        region = args[i + 1]
        args = args[:i] + args[i + 2:]
        apply_region(region)
    if '--overlay' in args:   # --overlay N data/exe/ovl/overlayN.exe
        i = args.index('--overlay')
        ovl = (int(args[i + 1]), args[i + 2])
        args = args[:i] + args[i + 3:]
    acebin, out_dir = args[0], args[1]
    ncases = int(args[2]) if len(args) > 2 else 12
    procs = int(args[3]) if len(args) > 3 else max(1, (os.cpu_count() or 2) - 1)
    stem = os.path.splitext(os.path.basename(ovl[1] if ovl else acebin))[0]
    meta = json.load(open(os.path.join(out_dir, stem + '.json')))
    only = [int(x, 16) for x in args[4:]]
    todo = [(m['addr'], ncases, 60000) for m in meta if not only or m['addr'] in only]
    names = {m['addr']: m['name'] for m in meta}
    ghidra = {m['addr'] for m in meta if m.get('ghidra')}
    results = {}
    pending = list(todo)
    while pending:   # si un proceso muere (fallo de C), se marca la funcion y se sigue
        with cf.ProcessPoolExecutor(procs, initializer=init, initargs=(acebin, out_dir, ovl, region)) as ex:
            futs = {ex.submit(check_func, t): t for t in pending}
            pending = []
            try:
                for k, fu in enumerate(cf.as_completed(futs)):
                    addr, verdict, counts, first = fu.result()
                    results[addr] = (verdict, counts, first)
                    if verdict == 'DISTINTA' and sum(1 for v in results.values() if v[0] == 'DISTINTA') <= 25:
                        print('  DISTINTA %08x %-24s %s' % (addr, names[addr], first), flush=True)
                    if len(results) % 200 == 0:
                        print('  ... %d/%d' % (len(results), len(todo)), flush=True)
            except cf.process.BrokenProcessPool:
                left = [t for t in futs.values() if t[0] not in results]
                running = [t for f, t in futs.items() if f.running() or (f.done() and f.exception())]
                for t in (running or left[:1]):
                    results[t[0]] = ('DISTINTA', {}, 'el proceso murio (fallo en el codigo C)')
                    print('  DISTINTA %08x %-24s proceso muerto' % (t[0], names[t[0]]), flush=True)
                pending = [t for t in left if t[0] not in results]
    tot = {v: sum(1 for r in results.values() if r[0] == v) for v in ('VALIDADA', 'DISTINTA', 'SIN CASO')}
    print('%d funciones: %d validadas, %d distintas, %d sin caso' % (len(results), tot['VALIDADA'], tot['DISTINTA'],
                                                                  tot['SIN CASO']))
    json.dump({'%08x' % a: {'nombre': names[a], 'veredicto': v[0], 'casos': v[1], 'primera_diferencia': v[2]}
               for a, v in sorted(results.items())}, open(os.path.join(out_dir, stem + '_check.json'), 'w'), indent=1)
    nval = sum(1 for a, r in results.items() if r[0] == 'VALIDADA' and a in ghidra)
    print('funciones de la lista de Ghidra validadas: %d de %d' % (nval, len(ghidra)))
    if not only and not region:   # progreso: funciones de Ghidra (las que cuenta tools/progress.py) validadas
        p = os.path.join(ROOT, 'docs', 'progreso.json')
        if os.path.exists(p):
            d = json.load(open(p))
            d.setdefault('recompiladas', {})['overlay %d' % ovl[0] if ovl else 'residente'] = nval
            json.dump(d, open(p, 'w'), indent=1, ensure_ascii=False)
    return 0


if __name__ == '__main__':
    sys.exit(main())
