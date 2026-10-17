#!/usr/bin/env python3
"""hle — enlace del interprete (tools/mipsemu.py) con la capa PsyQ de psx/ (etapa 6).

Cada funcion de libreria de Sony a la que salta el codigo del juego (155 destinos, con
nombre de Ghidra o identificadas a mano: tools/progress.py UNNAMED) se desvia a su
implementacion en C de psx/. Las firmas salen AUTOMATICAMENTE de los prototipos de
psx/include/*.h: un `*` es puntero (direccion PS1 -> puntero de C; NULL sigue NULL; si
se devuelve, de vuelta a direccion PS1), el resto se pasa con su tipo de C exacto (los
argumentos 5+ se leen de la pila, como en la ABI de MIPS). Casos aparte:
  - nombres mal firmados en el ACE.BIN (SsUtReverbOff = SpuInit, SpuRead = SpuWrite) y
    las funciones de archivo de la BIOS (open -> bios_open...);
  - printf/sprintf (argumentos variables: psx_format / psx_sformat);
  - memset, memchr, strcpy, strcat, strlen: sobre la RAM, como la libc;
  - __divdi3/__udivdi3: argumentos y resultado de 64 bits en parejas de registros;
  - __main: nada (el juego no tiene constructores).
Los callbacks que la capa entrega (VSync, CD, MDEC, eventos) vuelven al interprete por
psx_set_call (llamada reentrante).
"""
import ctypes
import glob
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from progress import UNNAMED  # noqa: E402

ALIAS = {'SsUtReverbOff': 'SpuInit', 'SpuRead': 'SpuWrite', 'rand': 'psx_rand', 'srand': 'psx_srand',
         'open': 'bios_open', 'read': 'bios_read', 'write': 'bios_write', 'close': 'bios_close',
         'firstfile': 'bios_firstfile', 'nextfile': 'bios_nextfile', 'erase': 'bios_erase'}
CTYPES = {'int': ctypes.c_int, 'long': ctypes.c_long, 'int32_t': ctypes.c_int32, 'uint32_t': ctypes.c_uint32,
          'int16_t': ctypes.c_int16, 'uint16_t': ctypes.c_uint16, 'uint8_t': ctypes.c_uint8, 'int8_t': ctypes.c_int8,
          'char': ctypes.c_char, 'unsigned': ctypes.c_uint, 'size_t': ctypes.c_size_t, 'CdlCB': ctypes.c_uint32,
          'int64_t': ctypes.c_int64, 'uint64_t': ctypes.c_uint64}


def parse_headers():
    """{nombre: (tipos de argumentos, tipo de retorno)}; tipo = ctypes o 'p' (puntero) o None"""
    sigs = {}
    for h in glob.glob(os.path.join(ROOT, 'psx', 'include', '*.h')):
        src = open(h).read()
        src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
        src = re.sub(r'#.*', ' ', src)
        src = re.sub(r'typedef[^;]*;', ' ', src)
        src = re.sub(r'\{[^{}]*\}', ' ', src)
        for m in re.finditer(r'([A-Za-z_][\w\s\*]*?)\b([A-Za-z_]\w*)\s*\(([^()]*)\)\s*;', src):
            ret, name, args = m.group(1).strip(), m.group(2), m.group(3).strip()
            def typ(t):
                t = t.replace('const', '').strip()
                if '*' in t:
                    return 'p'
                words = t.split()
                if not words or words == ['void']:
                    return None
                base = words[-2] if len(words) > 1 and words[-1] not in CTYPES else words[-1]
                if base == 'unsigned' or (len(words) > 1 and words[0] == 'unsigned'):
                    return ctypes.c_uint32
                return CTYPES.get(base, ctypes.c_int32)
            argl = [] if args in ('', 'void') else [typ(a) for a in args.split(',')]
            sigs[name] = (argl, typ(ret))
    return sigs


def lib_targets(acebin, ovls=(), region=None, ghidra_c=None):
    """{direccion: nombre} de las funciones de libreria a las que salta el juego. Con region
    (perfil de tools/recomp/region.py) y ghidra_c (su volcado DumpAll), de otra version"""
    d = open(acebin, 'rb').read()
    ghidra_c = ghidra_c or os.path.join(ROOT, 'data', 'ghidra', 'all_ace_eu.c')
    names = {int(a, 16): n for n, a in re.findall(r'// ==== (\S+) @ (\S+)\n',
                                                     open(ghidra_c, encoding='latin1').read())}
    if region:
        own = [(region['own_start'], region['lib'][0]), tuple(region['ulz'])]
        lib_lo, lib_hi, lib_main = region['lib'][0], region['lib'][1], region['extra_lib'][0]
        # las de libreria sin firma de Ghidra, por su direccion en esta version
        unnamed = {int(a, 16): n for a, n in region['unnamed'].items()}
        transmatrix = region['transmatrix']
    else:
        own = [(0x800252CC, 0x80099DE0), (0x800AE118, 0x800AE528)]
        lib_lo, lib_hi, lib_main = 0x80099DE0, 0x800AE09C, 0x80025214
        unnamed = None
        transmatrix = 0x800A53A8
    tg = set()

    def scan(buf, base, lo, hi):
        for a in range(lo, hi, 4):
            o = a - base
            if o < 0 or o + 4 > len(buf):
                break
            w = struct.unpack_from('<I', buf, o)[0]
            if w >> 26 in (2, 3):
                t = ((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
                if lib_lo <= t < lib_hi or t == lib_main:
                    tg.add(t)
    for lo, hi in own:
        scan(d, 0x80010000, lo, hi)
    for p in ovls:
        o = open(p, 'rb').read()
        base, size = struct.unpack_from('<II', o, 0x18)
        scan(o[2048:], base, base, base + size)
    out = {}
    for t in tg:
        n = names.get(t, '')
        if unnamed is not None and t in unnamed:
            n = unnamed[t]
        out[t] = UNNAMED.get(n, n) if n.startswith('FUN_') or not n else n
    out[transmatrix] = 'TransMatrix'
    return out


class Hle:
    def __init__(self, lib, machine, ram, scratch):
        self.lib, self.m = lib, machine
        self.base = ctypes.addressof(ram)
        self.sbase = ctypes.addressof(scratch)
        self.sigs = parse_headers()
        self.missing = set()
        self.calls = 0
        self.on_call = None        # gancho opcional (grabacion): f(addr, nombre, args, antes/despues)

    # direcciones PS1 <-> punteros de C
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
        if self.base <= p < self.base + 0x200000:
            return 0x80000000 | (p - self.base)
        return 0

    def arg(self, k):
        m = self.m
        return m.r[4 + k] if k < 4 else m.lw((m.r[29] + 16 + 4 * (k - 4)) & 0xFFFFFFFF)

    def cstr(self, a):
        out = bytearray()
        while a and len(out) < 4096:
            b = self.m.lb(a + len(out))
            if not b:
                break
            out.append(b)
        return bytes(out)

    def handler(self, name):
        m, lib = self.m, self.lib
        special = getattr(self, 'h_' + name, None)
        if special:
            return special
        cname = ALIAS.get(name, name)
        if cname not in self.sigs or not hasattr(lib, cname):
            self.missing.add(name)
            return None
        argt, rett = self.sigs[cname]
        f = getattr(lib, cname)
        f.argtypes = [ctypes.c_void_p if t == 'p' else t for t in argt]
        f.restype = ctypes.c_void_p if rett == 'p' else rett

        def h(mm):
            args = []
            for k, t in enumerate(argt):
                v = self.arg(k)
                if t == 'p':
                    args.append(self.host(v))
                elif t in (ctypes.c_int, ctypes.c_int32, ctypes.c_long, ctypes.c_int16, ctypes.c_int8):
                    args.append(struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0])
                elif t == ctypes.c_char:
                    args.append(bytes([v & 0xFF]))
                else:
                    args.append(v & 0xFFFFFFFF)
            r = f(*args)
            if rett == 'p':
                return self.ps1(r)
            if rett is None:
                return 0
            if isinstance(r, bytes):
                return r[0]
            return int(r) & 0xFFFFFFFF
        return h

    def install(self, targets):
        for addr, name in targets.items():
            h = self.handler(name)
            if h is None:
                continue

            def wrap(mm, h=h, addr=addr, name=name):
                self.calls += 1
                if self.on_call:
                    self.on_call(addr, name, mm, True)
                v = h(mm)
                if self.on_call:
                    self.on_call(addr, name, mm, False, v)
                return v
            self.m.hooks[addr] = wrap

    # ---- casos aparte ----
    def h___main(self, mm):
        return 0

    def _va(self, fmt_addr, first):
        fmt = self.cstr(fmt_addr)
        n = fmt.count(b'%') + 2
        vals = [self.arg(first + k) for k in range(n)]
        return fmt, (ctypes.c_uint32 * len(vals))(*vals)

    def h_printf(self, mm):
        fmt, arr = self._va(mm.r[4], 1)
        buf = ctypes.create_string_buffer(4096)
        self.lib.psx_format.restype = ctypes.c_int
        n = self.lib.psx_format(buf, 4096, fmt, arr)
        sys.stdout.write('[juego] ' + buf.value.decode('latin1'))
        return n

    def h_sprintf(self, mm):
        fmt, arr = self._va(mm.r[5], 2)
        buf = ctypes.create_string_buffer(4096)
        self.lib.psx_sformat.restype = ctypes.c_int
        n = self.lib.psx_sformat(buf, 4096, fmt, arr)
        out = buf.raw[:min(n, 4095)] + b'\0'
        for i, b in enumerate(out):
            mm.sb(mm.r[4] + i, b)
        return n

    def h_memset(self, mm):
        for i in range(mm.r[6]):
            mm.sb(mm.r[4] + i, mm.r[5])
        return mm.r[4]

    def h_memchr(self, mm):
        for i in range(mm.r[6]):
            if mm.lb(mm.r[4] + i) == mm.r[5] & 0xFF:
                return mm.r[4] + i
        return 0

    def h_strlen(self, mm):
        return len(self.cstr(mm.r[4]))

    def h_strcpy(self, mm):
        s = self.cstr(mm.r[5]) + b'\0'
        for i, b in enumerate(s):
            mm.sb(mm.r[4] + i, b)
        return mm.r[4]

    def h_strcat(self, mm):
        d = mm.r[4] + len(self.cstr(mm.r[4]))
        s = self.cstr(mm.r[5]) + b'\0'
        for i, b in enumerate(s):
            mm.sb(d + i, b)
        return mm.r[4]

    def h___divdi3(self, mm):
        a = (mm.r[5] << 32 | mm.r[4])
        b = (mm.r[7] << 32 | mm.r[6])
        a = a - (1 << 64) if a >> 63 else a
        b = b - (1 << 64) if b >> 63 else b
        q = 0 if b == 0 else (-(abs(a) // abs(b)) if (a < 0) != (b < 0) else abs(a) // abs(b))
        q &= 0xFFFFFFFFFFFFFFFF
        mm.r[3] = q >> 32
        return q & 0xFFFFFFFF

    def h___udivdi3(self, mm):
        a = (mm.r[5] << 32 | mm.r[4])
        b = (mm.r[7] << 32 | mm.r[6])
        q = 0 if b == 0 else a // b
        mm.r[3] = q >> 32
        return q & 0xFFFFFFFF
