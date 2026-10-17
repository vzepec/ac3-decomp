#!/usr/bin/env python3
"""psxcheck_card — valida la tarjeta de memoria de psx/ con el codigo de guardado del juego.

El interprete carga ACE.BIN y el overlay 1 (menus, 0x800C9DC0) y ejecuta sus rutinas de
tarjeta; las llamadas a la BIOS (open, read, write, close, firstfile, nextfile, eventos,
secciones criticas) y _card_format se desvian a psx/ (libcard.c, libapi.c). sprintf,
strcpy y memset corren tal cual. Secuencia, sobre una tarjeta nueva:
  1. FUN_800CC46C: abre los 7 eventos de tarjeta (SwCARD/HwCARD).
  2. FUN_800CD918(0): formatea el puerto 1.                       -> 1
  3. FUN_800CD968(0): lista "bu00:*.*".                           -> 15 bloques libres
  4. FUN_800CDAA8(0): crea BESCESP02066AC3PAL (1 bloque).         -> 1
  5. FUN_800CDCD4(0,1): guarda (escritura asincrona + evento).    -> 1; el archivo de la
     tarjeta contiene los 0x11FC bytes de la partida
  6. FUN_800CE0FC(0,1): carga sobre la zona borrada.              -> 1 y datos recuperados
  7. FUN_800CD968(0) otra vez: 1 archivo y 14 bloques libres.
Entre operaciones se limpian las banderas de "tarjeta ocupada" (+0x14CC/0x14CD) que en el
juego borra su maquina de estados por fotograma. Ademas se comprueba la imagen .mcd: firma
"MC", sumas XOR de los marcos de directorio y estado del bloque (0x51).

Uso:
    python3 tools/psxcheck_card.py data/exe/ace_eu.bin data/exe/ovl/overlay1.exe
"""
import ctypes
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsemu import Machine  # noqa: E402
from psyqcheck_gpu import build  # noqa: E402

BIOS = {0x800AD808: ('bios_open', 'pi', 'i'), 0x800AD818: ('bios_read', 'ipi', 'i'),
        0x800AD828: ('bios_write', 'ipi', 'i'), 0x800AD838: ('bios_close', 'i', 'i'),
        0x800ADA08: ('bios_firstfile', 'pp', 'p'), 0x800AD848: ('bios_nextfile', 'p', 'p'),
        0x800ABC38: ('_card_format', 'i', 'i'), 0x800AD758: ('OpenEvent', 'iiii', 'i'),
        0x800AD768: ('CloseEvent', 'i', 'i'), 0x800AD788: ('TestEvent', 'i', 'i'),
        0x800AD798: ('EnableEvent', 'i', 'i'), 0x800AD7A8: ('DisableEvent', 'i', 'i'),
        0x800AD778: ('WaitEvent', 'i', 'i'), 0x800AD7E8: ('EnterCriticalSection', '', 'v'),
        0x800AD7F8: ('ExitCriticalSection', '', 'v'), 0x800AB868: ('InitCARD', 'i', 'v'),
        0x800AB8F8: ('StartCARD', '', 'i'), 0x800AB940: ('StopCARD', '', 'i'), 0x800AB7D8: ('_bu_init', '', 'v')}
OPEN_EVENTS, FORMAT, LIST, CREATE, SAVE, LOAD = 0x800CC46C, 0x800CD918, 0x800CD968, 0x800CDAA8, 0x800CDCD4, 0x800CE0FC
PTR = 0x800D640C          # puntero al estado de la tarjeta del overlay (0x800D8970)
NAMES = 0x800C9E28        # "bu00:BESCESP02066AC3PAL" y "bu10:..." en los datos del overlay


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    lib = build()
    ram = (ctypes.c_uint8 * 0x200000).in_dll(lib, 'psx_ram')
    base = ctypes.addressof(ram)
    m = Machine.from_acebin(sys.argv[1], ram=ram)
    ovl = open(sys.argv[2], 'rb').read()
    addr, size = struct.unpack_from('<II', ovl, 0x18)
    m.ram[addr & 0x1FFFFF:(addr & 0x1FFFFF) + size] = ovl[2048:2048 + size]
    st = m.lw(PTR)

    def host(a):
        return ctypes.c_void_p(base + (a & 0x1FFFFF)) if a else None
    for a, (name, sig, ret) in BIOS.items():
        f = getattr(lib, name)
        f.restype = {'i': ctypes.c_int32, 'p': ctypes.c_void_p, 'v': None}[ret]

        def h(mm, f=f, sig=sig, ret=ret):
            args = [host(mm.r[4 + k]) if t == 'p' else ctypes.c_int32(struct.unpack('<i', struct.pack('<I', mm.r[4 + k]))[0])
                    for k, t in enumerate(sig)]
            r = f(*args)
            if ret == 'p':
                return 0x80000000 | (r - base) if r else 0
            return r or 0
        m.hooks[a] = h
    tmp = tempfile.mkdtemp()
    mcd = os.path.join(tmp, 'tarjeta1.mcd')
    lib.psx_card_attach(0, mcd.encode())
    lib.psx_card_detach(1)
    for k in range(2):   # nombres de los archivos, como la inicializacion del overlay
        nm = bytes(m.ram[(NAMES & 0x1FFFFF) + 24 * k:(NAMES & 0x1FFFFF) + 24 * k + 24]).split(b'\0')[0]
        m.ram[(st & 0x1FFFFF) + 0x1490 + 0x1A * k:(st & 0x1FFFFF) + 0x1490 + 0x1A * k + len(nm) + 1] = nm + b'\0'

    def idle():
        m.sb(st + 0x14CC, 0)
        m.sb(st + 0x14CD, 0)
        m.sh(st + 0x14E0, 0)

    def call(fn, args, frames=8):
        """llama y repite como lo haria el juego en fotogramas sucesivos hasta que acabe"""
        r = 0
        for _ in range(frames):
            idle()
            r = m.call(fn, args)
            r = struct.unpack('<i', struct.pack('<I', r & 0xFFFFFFFF))[0]
            if r != 0:
                break
        return r
    bad = 0
    m.call(OPEN_EVENTS, [])
    checks = []
    checks.append(('formatear', call(FORMAT, [0]), 1))
    checks.append(('listar (bloques libres)', call(LIST, [0]), 15))
    checks.append(('crear el archivo de la partida', call(CREATE, [0]), 1))
    data = bytes((i * 7 + 3) & 0xFF for i in range(0x11FC))
    m.ram[st & 0x1FFFFF:(st & 0x1FFFFF) + 0x11FC] = data
    r = call(SAVE, [0, 1])
    saved = bytes(m.ram[st & 0x1FFFFF:(st & 0x1FFFFF) + 0x11FC])   # el juego completa la cabecera al guardar
    lib.psx_card_image.restype = ctypes.c_void_p
    img = ctypes.string_at(lib.psx_card_image(0), 128 * 1024)
    checks.append(('guardar (escritura asincrona)', r, 1))
    checks.append(('datos en la tarjeta = memoria guardada', int(img[8192:8192 + 0x11FC] == saved), 1))
    m.ram[st & 0x1FFFFF:(st & 0x1FFFFF) + 0x11FC] = b'\0' * 0x11FC
    r = call(LOAD, [0, 1])
    loaded = bytes(m.ram[st & 0x1FFFFF:(st & 0x1FFFFF) + 0x11FC])
    checks.append(('cargar (devuelve)', r, None))
    checks.append(('datos cargados = guardados', int(loaded == saved), 1))
    checks.append(('listar otra vez (bloques libres)', call(LIST, [0]), 14))
    checks.append(('archivos listados', struct.unpack('<h', bytes(m.ram[(st & 0x1FFFFF) + 0x1234:(st & 0x1FFFFF) + 0x1236]))[0], 1))
    # formato de la imagen .mcd
    on_disk = open(mcd, 'rb').read()
    def xor_ok(fr):
        x = 0
        for b in fr[:127]:
            x ^= b
        return x == fr[127]
    xor_all = all(xor_ok(on_disk[k * 128:(k + 1) * 128]) for k in range(16))
    checks.append(('imagen .mcd: firma MC, sumas XOR, bloque 1 en uso con el nombre',
                   int(on_disk[:2] == b'MC' and xor_all and on_disk[128] == 0x51 and on_disk[138:138 + 18] == b'BESCESP02066AC3PAL'
                       and on_disk == img), 1))
    for name, got, want in checks:
        ok = want is None or got == want
        bad += not ok
        print('  %-58s %4s %s' % (name, got, 'bien' if ok else 'MAL (esperado %s)' % want))
    print('tarjeta (codigo del juego): %d fallos' % bad)
    print('TODO BIEN' if not bad else 'HAY FALLOS')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
