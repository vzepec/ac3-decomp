#!/usr/bin/env python3
"""psxcheck_io — valida libcd, libetc y libpad de psx/ ejecutando el CODIGO DEL JUEGO.

El interprete MIPS (tools/mipsemu.py) ejecuta las funciones originales de ACE.BIN sobre
la MISMA RAM que la capa de C (psx_ram por ctypes). Las llamadas del juego a las
funciones de libreria (CdControlF, CdGetSector, VSync, PadGetState...) se desvian a la
implementacion de psx/ (hooks), y los callbacks que la capa entrega (CdSyncCallback,
CdReadyCallback) vuelven al interprete. Comprobaciones:

  1. CdIntToPos / CdPosToInt: frente a las originales (oraculo, valores aleatorios).
  2. CdSearchFile frente al ISO9660 leido por tools/psxiso.py.
  3. packs_init (0x8002AC0C) del juego: busca los 4 packs, lee los indices con CdRead y
     los valida (marca AC3E y codigos de build). Sin error y con las tablas reubicadas
     identicas al indice leido por tools/acepack.py.
  4. Lectura de registros por streaming con el lector del juego (spb_stream_start,
     0x8002A34C: Setloc, Setmode 0xA0, ReadN, un callback por sector con CdGetSector,
     Pause): el contenido debe ser identico a data/bpb/NNNN_*.bin (extraidos por
     tools/acepack.py), y el tiempo, 3 sectores por fotograma (doble velocidad, PAL).
  5. Mando: inicializacion (0x800260AC) y lectura por fotograma (0x800269E4) del juego
     sobre los buferes de PadInitDirect: botones, pulsaciones nuevas y ejes.

Uso:
    python3 tools/psxcheck_io.py <imagen.bin> data/exe/ace_eu.bin [registros...]
"""
import ctypes
import glob
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsemu import Machine  # noqa: E402
from psyqcheck_gpu import build  # noqa: E402
from acepack import load  # noqa: E402
from psxiso import Disc  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# funciones de libreria del ACE.BIN europeo (firmas PsyQ, data/ghidra/all_ace_eu.c)
LIB = {
    'VSync': 0x800A0568, 'ResetCallback': 0x800A0778, 'DMACallback': 0x800A07D8, 'VSyncCallback': 0x800A0808,
    'StopCallback': 0x800A086C, 'CheckCallback': 0x800A08CC, 'SetIntrMask': 0x800A08F4,
    'CdInit': 0x800A1228, 'CdStatus': 0x800A1378, 'CdMode': 0x800A1388, 'CdLastCom': 0x800A1398,
    'CdLastPos': 0x800A13A8, 'CdReset': 0x800A13B4, 'CdFlush': 0x800A1420, 'CdSetDebug': 0x800A1440,
    'CdSync': 0x800A14BC, 'CdReady': 0x800A14DC, 'CdSyncCallback': 0x800A14FC, 'CdReadyCallback': 0x800A1510,
    'CdControl': 0x800A1524, 'CdControlF': 0x800A1660, 'CdControlB': 0x800A1794, 'CdMix': 0x800A18E0,
    'CdGetSector': 0x800A1900, 'CdGetSector2': 0x800A1920, 'CdDataCallback': 0x800A1940,
    'CdDataSync': 0x800A1964, 'CdSearchFile': 0x800A30F8, 'CdRead': 0x800A4168, 'CdReadSync': 0x800A4304,
    'CdReadCallback': 0x800A4408, 'CdIntToPos': 0x800A4438, 'CdPosToInt': 0x800A453C,
    'PadEnableCom': 0x80099DE0, 'PadChkVsync': 0x8009B1E0, 'PadStartCom': 0x8009B200, 'PadStopCom': 0x8009B220,
    'PadGetState': 0x8009B28C, 'PadInfoMode': 0x8009B358, 'PadInfoAct': 0x8009B450,
    'PadSetActAlign': 0x8009B5CC, 'PadSetMainMode': 0x8009B604, 'PadSetAct': 0x8009B64C,
    'PadInitDirect': 0x8009C2A0,
}
# firma: p = puntero (direccion PS1 -> puntero de C), i = entero; retorno: p, i o v
SIG = {
    'VSync': 'i>i', 'ResetCallback': '>i', 'DMACallback': 'ii>i', 'VSyncCallback': 'i>i', 'StopCallback': '>i',
    'CheckCallback': '>i', 'SetIntrMask': 'i>i', 'CdInit': '>i', 'CdStatus': '>i', 'CdMode': '>i',
    'CdLastCom': '>i', 'CdLastPos': '>p', 'CdReset': 'i>i', 'CdFlush': '>v', 'CdSetDebug': 'i>i',
    'CdSync': 'ip>i', 'CdReady': 'ip>i', 'CdSyncCallback': 'i>i', 'CdReadyCallback': 'i>i',
    'CdControl': 'ipp>i', 'CdControlF': 'ip>i', 'CdControlB': 'ipp>i', 'CdMix': 'p>i', 'CdGetSector': 'pi>i',
    'CdGetSector2': 'pi>i', 'CdDataCallback': 'i>i', 'CdDataSync': 'i>i', 'CdSearchFile': 'pp>p',
    'CdRead': 'ipi>i', 'CdReadSync': 'ip>i', 'CdReadCallback': 'i>i', 'CdIntToPos': 'ip>p', 'CdPosToInt': 'p>i',
    'PadEnableCom': 'i>i', 'PadChkVsync': '>i', 'PadStartCom': '>v', 'PadStopCom': '>v', 'PadGetState': 'i>i',
    'PadInfoMode': 'iii>i', 'PadInfoAct': 'iii>i', 'PadSetActAlign': 'ip>i', 'PadSetMainMode': 'iii>i',
    'PadSetAct': 'ipi>v', 'PadInitDirect': 'pp>v',
}
PRINTF, PUTS, SPU_SET_COMMON_ATTR = 0x800AC3E8, 0x800AD618, 0x8009F888
PACKS_INIT, STREAM_START, TOC_ENTRY = 0x8002AC0C, 0x8002A34C, 0x8002953C
PAD_INIT, PAD_FRAME = 0x800260AC, 0x800269E4
PTR_CDSTATE = 0x800BAA28          # puntero al estado del lector de CD del juego
DONE_CB = 0x8000FF00              # callback de fin de lectura (desviado a Python)
FRAMES = 0x801E0000               # contador de fotogramas que recibe packs_init
DEST = 0x80100000                 # destino de las lecturas de registros
PAD_OUT = 0x801E1000              # estructuras de salida de los mandos (8 x 0x1A)


class Harness:
    def __init__(self, image, acebin):
        self.lib = lib = build()
        self.ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
        self.scr = (ctypes.c_uint8 * 0x400).in_dll(lib, 'psx_scratch')
        self.base = ctypes.addressof(self.ram)
        self.sbase = ctypes.addressof(self.scr)
        self.m = Machine.from_acebin(acebin, ram=self.ram, scratch=self.scr)
        if lib.psx_cd_open(image.encode()) != 0:
            raise SystemExit('no se abre la imagen')
        lib.psx_cd_stats.restype = ctypes.POINTER(ctypes.c_uint32 * 5)
        self.log = []
        self.done = None
        self.call_t = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)(self.dispatch)
        lib.psx_set_call(self.call_t)

    # direccion PS1 <-> puntero de C
    def host(self, a):
        if not a:
            return None
        if (a & 0xFFFFFC00) == 0x1F800000:
            return ctypes.c_void_p(self.sbase + (a & 0x3FF))
        return ctypes.c_void_p(self.base + (a & 0x1FFFFF))

    def ps1(self, p):
        if not p:
            return 0
        if self.sbase <= p < self.sbase + 0x400:
            return 0x1F800000 + p - self.sbase
        return 0x80000000 | (p - self.base)

    def dispatch(self, addr, a0, a1):
        """callback de la capa de C -> codigo MIPS del juego (o Python si es DONE_CB)"""
        if addr == DONE_CB:
            self.done = struct.unpack('<i', struct.pack('<I', a0))[0]
            return
        self.m.call(addr, [a0, a1])

    def hook_all(self):
        m, lib = self.m, self.lib
        for name, addr in LIB.items():
            args, ret = SIG[name].split('>')
            f = getattr(lib, name)
            f.restype = {'p': ctypes.c_void_p, 'i': ctypes.c_int32, 'v': None}[ret]

            def h(mm, f=f, args=args, ret=ret, name=name):
                vals = []
                for k, t in enumerate(args):
                    v = mm.r[4 + k] if k < 4 else mm.lw(mm.r[29] + 16 + 4 * (k - 4))
                    vals.append(self.host(v) if t == 'p' else ctypes.c_int32(struct.unpack('<i', struct.pack('<I', v))[0]))
                r = f(*vals)
                return self.ps1(r) if ret == 'p' else (r or 0)
            m.hooks[addr] = h
        m.hooks[PRINTF] = lambda mm: self.log.append(self.cstr(mm.r[4])) or 0
        m.hooks[PUTS] = lambda mm: self.log.append(self.cstr(mm.r[4])) or 0
        m.hooks[SPU_SET_COMMON_ATTR] = lambda mm: 0   # libspu: paso 8
        m.hooks[DONE_CB] = self.done_hook             # el juego lo llama con un salto indirecto

    def done_hook(self, mm):
        self.done = struct.unpack('<i', struct.pack('<I', mm.r[4]))[0]
        return 0

    def cstr(self, a):
        out = bytearray()
        while self.m.lb(a + len(out)) and len(out) < 200:
            out.append(self.m.lb(a + len(out)))
        return out.decode('latin1')


def check_pos(h, rnd, n=20000):
    """1. CdIntToPos / CdPosToInt frente al codigo original (sin hooks)"""
    m, lib = h.m, h.lib
    lib.CdIntToPos.restype = ctypes.c_void_p
    lib.CdPosToInt.restype = ctypes.c_int32
    a = 0x80190000
    bad = 0
    for _ in range(n):
        i = rnd.choice((rnd.randrange(0, 330000), rnd.randrange(-200, 200), rnd.getrandbits(31) - (1 << 30)))
        m.sw(a, 0x5A5A5A5A)
        m.call(LIB['CdIntToPos'], [i & 0xFFFFFFFF, a])
        o = bytes(m.ram[a & 0x1FFFFF:(a & 0x1FFFFF) + 4])
        m.sw(a, 0x5A5A5A5A)
        lib.CdIntToPos(ctypes.c_int32(i), h.host(a))
        c = bytes(m.ram[a & 0x1FFFFF:(a & 0x1FFFFF) + 4])
        loc = bytes(rnd.randrange(256) for _ in range(4))
        m.ram[(a & 0x1FFFFF) + 8:(a & 0x1FFFFF) + 12] = loc
        po = m.call(LIB['CdPosToInt'], [a + 8])
        pc = lib.CdPosToInt(h.host(a + 8))
        bad += (o != c) + (po != pc)
    print('CdIntToPos/CdPosToInt: %d casos, %d diferencias' % (2 * n, bad))
    return bad


def check_search(h, image):
    """2. CdSearchFile frente a tools/psxiso.py"""
    disc = Disc(image)
    bad = 0
    names = ['ACE.SPH', 'ACE.SPB', 'ACE.BPH', 'ACE.BPB', 'SYSTEM.CNF']
    h.lib.CdSearchFile.restype = ctypes.c_void_p
    for nm in names:
        e = disc.find(nm)
        a = 0x80190100
        r = h.lib.CdSearchFile(h.host(a), ('\\%s;1' % nm).encode())
        if not r:
            print('  %s: no encontrado' % nm)
            bad += 1
            continue
        size = h.m.lw(a + 4)
        lba = h.lib.CdPosToInt(h.host(a))
        if e is not None:
            ok = (lba, size) == (e.lba, e.size)
            bad += not ok
            print('  %-10s LBA %6d, %9d B %s' % (nm, lba, size, 'coincide con psxiso' if ok else 'DISTINTO de psxiso %d %d' % (e.lba, e.size)))
    return bad


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    image, acebin = sys.argv[1], sys.argv[2]
    recs = [int(x) for x in sys.argv[3:]] or [0, 1, 6, 48, 126]
    h = Harness(image, acebin)
    rnd = random.Random(11)
    fails = check_pos(h, rnd)
    fails += check_search(h, image)

    # 3. packs_init del juego
    h.hook_all()
    m, lib = h.m, h.lib
    lib.CdInit()
    lib.psx_set_refresh(50)
    m.sw(FRAMES, 0)
    m.call(PACKS_INIT, [FRAMES])
    st = m.lw(PTR_CDSTATE)
    err = m.lw(st + 0xC)
    bph_tab = m.lw(st + 0x18)
    disc = Disc(image)
    idx, pack = load(disc, 'BPB')
    ents = [(m.lw(bph_tab + 8 * i), m.lw(bph_tab + 8 * i + 4)) for i in range(idx.count)]
    want = [(e[0], e[1] + pack.lba) for e in idx.entries]
    nbph = sum(a == b for a, b in zip(ents, want))
    # +0x0C: 5 = error; si no, el codigo copia ahi el campo 'kind' del SPH (1)
    print('packs_init (codigo del juego): estado %d (5 = error; 1 = kind del SPH), indice BPH %d/%d entradas '
          'identicas, mensajes %s' % (err, nbph, idx.count, h.log or 'ninguno'))
    fails += err != 1 or nbph != idx.count or bool(h.log)

    # 4. registros por streaming con el lector del juego
    for k in recs:
        path = glob.glob(os.path.join(ROOT, 'data', 'bpb', '%04d_*' % k))
        if not path:
            print('registro %d: falta data/bpb (make extract-bpb)' % k)
            fails += 1
            continue
        want = open(path[0], 'rb').read()
        entry = m.call(TOC_ENTRY, [k]) & 0xFFFFFFFF
        # el archivo extraido llega hasta el final del sector; el juego copia el tamano exacto del tag
        want = want[:(m.lw(entry) & 0x7FFFF) * 4]
        m.ram[DEST & 0x1FFFFF:(DEST & 0x1FFFFF) + len(want) + 4] = b'\xEE' * (len(want) + 4)
        h.done = None
        s0 = list(lib.psx_cd_stats().contents)
        r = m.call(STREAM_START, [entry, DEST, 0, DONE_CB, 0])
        frames = 0
        while h.done is None and frames < 5000:
            lib.psx_vblank()
            m.sw(FRAMES, m.lw(FRAMES) + 1)
            frames += 1
        s1 = list(lib.psx_cd_stats().contents)
        got = bytes(m.ram[DEST & 0x1FFFFF:(DEST & 0x1FFFFF) + len(want)])
        over = m.lb(DEST + len(want))   # sin escribir mas alla del final
        nsec = s1[2] - s0[2]
        # doble velocidad en PAL: 150 sectores/s a 50 fotogramas/s = 3 por fotograma (+ arranque)
        timing = nsec == (len(want) + 2047) // 2048 and nsec / 3 <= frames <= nsec / 3 + 3
        ok = r == 1 and h.done == 0 and got == want and over == 0xEE and timing
        fails += not ok
        print('registro %3d: %7d B, %4d sectores en %4d fotogramas (%.2f/fotograma), %d callbacks, fin=%s: %s'
              % (k, len(want), s1[2] - s0[2], frames, (s1[2] - s0[2]) / max(frames, 1), s1[4] - s0[4], h.done,
                 'IDENTICO' if ok else 'DISTINTO (inicio %s)' % r))

    # 5. mando
    fails += check_pad(h)
    print('TODO BIEN' if not fails else 'HAY FALLOS (%d)' % fails)
    return 1 if fails else 0


def check_pad(h):
    m, lib = h.m, h.lib
    for i in range(0x1A * 8):
        m.sb(PAD_OUT + i, 0)
    lib.psx_pad_connect(0, 0x41)
    lib.psx_pad_connect(1, 0)
    m.call(PAD_INIT, [PAD_OUT, 0, 0, 0])
    X, ST, UP = 1 << 14, 1 << 3, 1 << 4
    steps = [  # (id, botones, lx, ly, rx, ry) -> esperado (botones, nuevos, ejes 0x12..0x18)
        (0x41, X, 0x80, 0x80, 0x80, 0x80, X, X, None),
        (0x41, X | ST, 0x80, 0x80, 0x80, 0x80, X | ST, ST, None),
        (0x41, 0, 0x80, 0x80, 0x80, 0x80, 0, 0, None),
        (0x73, UP, 0x11, 0x22, 0x33, 0x44, UP, UP, (0x44, 0x33, 0x22, 0x11)),
    ]
    bad = 0
    for n, (pid, b, lx, ly, rx, ry, eb, en, axes) in enumerate(steps):
        lib.psx_pad_connect(0, pid)
        lib.psx_pad_set(0, b, lx, ly, rx, ry)
        for _ in range(2 if n == 3 else 1):   # al cambiar de tipo, un fotograma para renegociar
            lib.psx_vblank()
            m.call(PAD_FRAME, [])
        o = PAD_OUT
        gb, gn = m.lh(o + 0xE), m.lh(o + 0x10)
        ga = tuple(m.lh(o + 0x12 + 2 * k) for k in range(4))
        ok = gb == eb and (gn == en or n == 3) and (axes is None or ga == axes) and m.lb(o + 0xC) == pid
        bad += not ok
        print('  mando, paso %d: id %02x botones %04x nuevos %04x ejes %s -> %s'
              % (n, m.lb(o + 0xC), gb, gn, ['%02x' % v for v in ga], 'bien' if ok else 'MAL'))
    lib.psx_pad_connect(0, 0)
    lib.psx_vblank()
    m.call(PAD_FRAME, [])
    ok = m.lh(PAD_OUT + 0xE) == 0 and m.lb(PAD_OUT + 0xD) != 0
    bad += not ok
    print('  mando, desconectado: botones %04x estado %d -> %s' % (m.lh(PAD_OUT + 0xE), m.lb(PAD_OUT + 0xD),
                                                                   'bien' if ok else 'MAL'))
    print('mando (codigo del juego): %d fallos' % bad)
    return bad


if __name__ == '__main__':
    sys.exit(main())
