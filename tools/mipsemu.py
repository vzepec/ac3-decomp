#!/usr/bin/env python3
"""mipsemu — interprete MIPS R3000 minimo para ejecutar funciones de ACE.BIN.

Sirve de ORACULO en la etapa 5: ejecuta las funciones originales de las
librerias de Sony que estan dentro de ACE.BIN (rsin, ratan2, SquareRoot0,
rand...) para comparar sus resultados con la reimplementacion en C de psx/.

Alcance: instrucciones enteras del R3000 (con hueco de retardo en saltos y
cargas sin interbloqueo modelado), RAM de 2 MB en 0x80000000 (con espejos en
0x00000000 y 0xA0000000) y scratchpad de 1 KB en 0x1F800000. De la GTE solo
los movimientos de registros (MFC2/CFC2/MTC2/CTC2) y el contador LZCS/LZCR;
las ordenes de calculo, COP0 y las llamadas al sistema detienen la ejecucion
con un error: las funciones que calculan con la GTE se validan aparte.

Uso como modulo:
    from mipsemu import Machine
    m = Machine.from_acebin('data/exe/ace_eu.bin')
    m.call(0x800A45C8, [1024])        # rsin(1024) -> 4096

Uso por consola (prueba rapida):
    python3 tools/mipsemu.py data/exe/ace_eu.bin 0x800A45C8 1024
"""
import struct
import sys

RAM_SIZE = 2 * 1024 * 1024
RETURN_MAGIC = 0x8000FFF0   # direccion de retorno ficticia: al saltar aqui, la llamada acaba
GP_EU = 0x800BA9F4


class MipsError(Exception):
    pass


class PsxGteLib:
    """La GTE de psx/ (psx/src/gte.c) vista desde Python por ctypes."""

    def __init__(self, lib):
        import ctypes
        self.ct = ctypes
        self.lib = lib
        self.ctx = ctypes.create_string_buffer(256)    # PsxGte: 64 registros de 32 bits
        lib.psx_gte_reset(self.ctx)
        for name, res in (('psx_gte_read_data', ctypes.c_uint32), ('psx_gte_read_ctrl', ctypes.c_uint32)):
            getattr(lib, name).restype = res

    def read_data(self, r): return self.lib.psx_gte_read_data(self.ctx, r)
    def read_ctrl(self, r): return self.lib.psx_gte_read_ctrl(self.ctx, r)
    def write_data(self, r, v): self.lib.psx_gte_write_data(self.ctx, r, self.ct.c_uint32(v & 0xFFFFFFFF))
    def write_ctrl(self, r, v): self.lib.psx_gte_write_ctrl(self.ctx, r, self.ct.c_uint32(v & 0xFFFFFFFF))
    def command(self, c): self.lib.psx_gte_command(self.ctx, self.ct.c_uint32(c))
    def state(self): return bytes(self.ctx.raw)
    def set_state(self, raw): self.ct.memmove(self.ctx, raw, 256)


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


class Machine:
    def __init__(self, gte=None):
        """gte: objeto con read_data/write_data/read_ctrl/write_ctrl/command (p. ej.
        PsxGteLib, la GTE de psx/ por ctypes). Sin el, solo movimientos y LZCS/LZCR."""
        self.gte = gte
        self.ram = bytearray(RAM_SIZE)
        self.scratch = bytearray(1024)
        self.r = [0] * 32
        self.hi = self.lo = 0
        self.c2d = [0] * 32         # registros de datos de la GTE (solo movimientos y LZCS/LZCR)
        self.c2c = [0] * 32
        self.steps = 0

    @classmethod
    def from_acebin(cls, path, base=0x80010000, gp=GP_EU, gte=None):
        m = cls(gte)
        d = open(path, 'rb').read()
        if d[:8] == b'PS-X EXE':
            base = struct.unpack_from('<I', d, 0x18)[0]
            d = d[2048:]
        off = base & 0x1FFFFF
        m.ram[off:off + len(d)] = d
        m.gp = gp
        return m

    # ---- memoria ----
    def _region(self, addr):
        a = addr & 0xFFFFFFFF
        if 0x1F800000 <= a < 0x1F800400:
            return self.scratch, a - 0x1F800000
        seg = a & 0xE0000000
        if seg in (0x00000000, 0x80000000, 0xA0000000) and (a & 0x1FFFFFFF) < 0x800000:
            return self.ram, a & 0x1FFFFF
        raise MipsError('acceso fuera de memoria: %08x' % a)

    def lw(self, a):
        if a & 3:
            raise MipsError('lw desalineado %08x' % a)
        b, o = self._region(a)
        return struct.unpack_from('<I', b, o)[0]

    def lh(self, a):
        b, o = self._region(a)
        return struct.unpack_from('<H', b, o)[0]

    def lb(self, a):
        b, o = self._region(a)
        return b[o]

    def sw(self, a, v):
        if a & 3:
            raise MipsError('sw desalineado %08x' % a)
        b, o = self._region(a)
        struct.pack_into('<I', b, o, v & 0xFFFFFFFF)

    def sh(self, a, v):
        b, o = self._region(a)
        struct.pack_into('<H', b, o, v & 0xFFFF)

    def sb(self, a, v):
        b, o = self._region(a)
        b[o] = v & 0xFF

    # ---- ejecucion ----
    def call(self, addr, args=(), max_steps=2000000):
        """Llama a la funcion `addr` (argumentos 5+ en la pila, como en MIPS O32); devuelve v0."""
        r = self.r
        for i in range(32):
            r[i] = 0
        for i, v in enumerate(args[:4]):
            r[4 + i] = v & 0xFFFFFFFF
        r[28] = self.gp
        r[29] = 0x801FFF00          # pila al final de la RAM
        r[31] = RETURN_MAGIC
        for k, v in enumerate(args[4:]):      # argumentos 5+ en la pila (sp + 16 + 4k)
            self.sw(r[29] + 16 + 4 * k, v)
        pc, npc = addr, addr + 4
        steps = 0
        while pc != RETURN_MAGIC:
            steps += 1
            if steps > max_steps:
                raise MipsError('demasiados pasos (bucle?) en %08x' % pc)
            ins = self.lw(pc)
            target = self._exec(ins, pc, npc)
            pc, npc = npc, (target if target is not None else npc + 4)
            r[0] = 0
        self.steps = steps
        return s32(r[2])

    def _exec(self, ins, pc, npc):
        r = self.r
        op = ins >> 26
        rs, rt, rd = (ins >> 21) & 31, (ins >> 16) & 31, (ins >> 11) & 31
        sh = (ins >> 6) & 31
        imm = ins & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 0:
            fn = ins & 63
            if fn == 0x00: r[rd] = (r[rt] << sh) & 0xFFFFFFFF
            elif fn == 0x02: r[rd] = r[rt] >> sh
            elif fn == 0x03: r[rd] = (s32(r[rt]) >> sh) & 0xFFFFFFFF
            elif fn == 0x04: r[rd] = (r[rt] << (r[rs] & 31)) & 0xFFFFFFFF
            elif fn == 0x06: r[rd] = r[rt] >> (r[rs] & 31)
            elif fn == 0x07: r[rd] = (s32(r[rt]) >> (r[rs] & 31)) & 0xFFFFFFFF
            elif fn == 0x08: return r[rs]
            elif fn == 0x09:
                t = r[rs]
                r[rd] = npc + 4
                return t
            elif fn in (0x0C, 0x0D): raise MipsError('syscall/break en %08x' % pc)
            elif fn == 0x10: r[rd] = self.hi
            elif fn == 0x11: self.hi = r[rs]
            elif fn == 0x12: r[rd] = self.lo
            elif fn == 0x13: self.lo = r[rs]
            elif fn == 0x18:
                p = s32(r[rs]) * s32(r[rt])
                self.lo, self.hi = p & 0xFFFFFFFF, (p >> 32) & 0xFFFFFFFF
            elif fn == 0x19:
                p = r[rs] * r[rt]
                self.lo, self.hi = p & 0xFFFFFFFF, (p >> 32) & 0xFFFFFFFF
            elif fn == 0x1A:
                a, b = s32(r[rs]), s32(r[rt])
                if b == 0:   # comportamiento del R3000 con divisor 0
                    self.lo, self.hi = (1 if a < 0 else 0xFFFFFFFF), a & 0xFFFFFFFF
                elif a == -0x80000000 and b == -1:
                    self.lo, self.hi = 0x80000000, 0
                else:
                    q = abs(a) // abs(b)
                    if (a < 0) != (b < 0):
                        q = -q
                    self.lo, self.hi = q & 0xFFFFFFFF, (a - q * b) & 0xFFFFFFFF
            elif fn == 0x1B:
                a, b = r[rs], r[rt]
                if b == 0:
                    self.lo, self.hi = 0xFFFFFFFF, a
                else:
                    self.lo, self.hi = a // b, a % b
            elif fn in (0x20, 0x21): r[rd] = (r[rs] + r[rt]) & 0xFFFFFFFF
            elif fn in (0x22, 0x23): r[rd] = (r[rs] - r[rt]) & 0xFFFFFFFF
            elif fn == 0x24: r[rd] = r[rs] & r[rt]
            elif fn == 0x25: r[rd] = r[rs] | r[rt]
            elif fn == 0x26: r[rd] = r[rs] ^ r[rt]
            elif fn == 0x27: r[rd] = ~(r[rs] | r[rt]) & 0xFFFFFFFF
            elif fn == 0x2A: r[rd] = int(s32(r[rs]) < s32(r[rt]))
            elif fn == 0x2B: r[rd] = int(r[rs] < r[rt])
            else: raise MipsError('SPECIAL %02x no soportada en %08x' % (fn, pc))
            return None
        if op == 1:
            cond = s32(r[rs]) < 0 if rt & 1 == 0 else s32(r[rs]) >= 0
            if rt & 0x10:
                r[31] = npc + 4
            return (npc + (simm << 2)) & 0xFFFFFFFF if cond else None
        if op == 2: return (npc & 0xF0000000) | ((ins & 0x3FFFFFF) << 2)
        if op == 3:
            r[31] = npc + 4
            return (npc & 0xF0000000) | ((ins & 0x3FFFFFF) << 2)
        if op == 4: return (npc + (simm << 2)) & 0xFFFFFFFF if r[rs] == r[rt] else None
        if op == 5: return (npc + (simm << 2)) & 0xFFFFFFFF if r[rs] != r[rt] else None
        if op == 6: return (npc + (simm << 2)) & 0xFFFFFFFF if s32(r[rs]) <= 0 else None
        if op == 7: return (npc + (simm << 2)) & 0xFFFFFFFF if s32(r[rs]) > 0 else None
        if op in (8, 9): r[rt] = (r[rs] + simm) & 0xFFFFFFFF
        elif op == 0x0A: r[rt] = int(s32(r[rs]) < simm)
        elif op == 0x0B: r[rt] = int(r[rs] < (simm & 0xFFFFFFFF))
        elif op == 0x0C: r[rt] = r[rs] & imm
        elif op == 0x0D: r[rt] = r[rs] | imm
        elif op == 0x0E: r[rt] = r[rs] ^ imm
        elif op == 0x0F: r[rt] = (imm << 16) & 0xFFFFFFFF
        elif op == 0x12 and self.gte is not None:
            g = self.gte
            if ins & 0x02000000: g.command(ins & 0x1FFFFFF)
            elif rs == 0: r[rt] = g.read_data(rd)
            elif rs == 2: r[rt] = g.read_ctrl(rd)
            elif rs == 4: g.write_data(rd, r[rt])
            elif rs == 6: g.write_ctrl(rd, r[rt])
            else: raise MipsError('COP2 rs=%d en %08x' % (rs, pc))
            return None
        elif op == 0x12 and not ins & 0x02000000 and rs in (0, 2, 4, 6):
            # movimientos de registros de la GTE; LZCS (30) -> LZCR (31) = bits iniciales iguales al signo
            if rs == 0: r[rt] = self.c2d[rd]
            elif rs == 2: r[rt] = self.c2c[rd]
            elif rs == 6: self.c2c[rd] = r[rt]
            else:
                self.c2d[rd] = r[rt]
                if rd == 30:
                    v = r[rt]
                    if v & 0x80000000:
                        v = ~v & 0xFFFFFFFF
                    n = 0
                    while n < 32 and not (v << n) & 0x80000000:
                        n += 1
                    self.c2d[31] = n
            return None
        elif op in (0x10, 0x12): raise MipsError('COP%d (orden) en %08x' % (op - 0x10, pc))
        else:
            a = (r[rs] + simm) & 0xFFFFFFFF
            if op == 0x20: v = self.lb(a); r[rt] = (v - 256 if v & 0x80 else v) & 0xFFFFFFFF
            elif op == 0x21: v = self.lh(a); r[rt] = (v - 0x10000 if v & 0x8000 else v) & 0xFFFFFFFF
            elif op == 0x23: r[rt] = self.lw(a)
            elif op == 0x24: r[rt] = self.lb(a)
            elif op == 0x25: r[rt] = self.lh(a)
            elif op == 0x22:   # lwl
                w = self.lw(a & ~3); k = a & 3
                r[rt] = ((r[rt] & (0x00FFFFFF >> (8 * k))) | (w << (8 * (3 - k)))) & 0xFFFFFFFF
            elif op == 0x26:   # lwr
                w = self.lw(a & ~3); k = a & 3
                r[rt] = ((r[rt] & ((0xFFFFFF00 << (8 * (3 - k))) & 0xFFFFFFFF)) | (w >> (8 * k))) & 0xFFFFFFFF
            elif op == 0x28: self.sb(a, r[rt])
            elif op == 0x29: self.sh(a, r[rt])
            elif op == 0x2B: self.sw(a, r[rt])
            elif op == 0x2A:   # swl
                w = self.lw(a & ~3); k = a & 3
                w = (w & ((0xFFFFFF00 << (8 * k)) & 0xFFFFFFFF)) | (r[rt] >> (8 * (3 - k)))
                self.sw(a & ~3, w)
            elif op == 0x2E:   # swr
                w = self.lw(a & ~3); k = a & 3
                w = (w & (0x00FFFFFF >> (8 * (3 - k)))) | ((r[rt] << (8 * k)) & 0xFFFFFFFF)
                self.sw(a & ~3, w)
            elif op == 0x32 and self.gte is not None: self.gte.write_data(rt, self.lw(a))
            elif op == 0x3A and self.gte is not None: self.sw(a, self.gte.read_data(rt))
            elif op in (0x32, 0x3A): raise MipsError('LWC2/SWC2 en %08x' % pc)
            else: raise MipsError('opcode %02x no soportado en %08x' % (op, pc))
        return None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    m = Machine.from_acebin(sys.argv[1])
    addr = int(sys.argv[2], 0)
    args = [int(x, 0) for x in sys.argv[3:]]
    v = m.call(addr, args)
    print('%#010x(%s) = %d (%#x), %d instrucciones' % (addr, ', '.join(map(str, args)), v, v & 0xFFFFFFFF, m.steps))


if __name__ == '__main__':
    main()
