#!/usr/bin/env python3
"""region — perfil de direcciones de una version de ACE.BIN (EU o US) para la recompilacion.

La recompilacion (tools/recomp/) y los arranques nativos (psx/recomp/native_main.c,
android_game/) se escribieron con las direcciones del ACE.BIN europeo. La version americana
tiene el mismo motor pero otro reparto: el codigo propio cambia (deltas de 0x154 a 0x3D8
segun la zona), la libreria de Sony va entera desplazada 0x3D4 y la BSS y los overlays
empiezan en otro sitio. Este programa SACA del binario de la version pedida cada direccion
que necesitan las herramientas, sin escribir ninguna a mano:

  - del arranque (crt0, en la entrada del ejecutable): limites de la BSS, $gp, base de los
    overlays (= fin de la BSS), direccion de ac3_main (el jal tras restaurar $ra), __main,
    stup1/stup0 (los nombres que pone Ghidra) y el inicio del codigo propio;
  - de la tabla de modos (se busca: registros de 16 bytes con id de overlay 1-5 cargado en
    la base de los overlays): el final de cada overlay;
  - por FIRMA de codigo frente al ACE.BIN europeo (ventanas de instrucciones con las
    direcciones enmascaradas: inmediatos de lui/addiu/cargas y destinos de jal): las
    direcciones de las funciones de libreria sin nombre de Ghidra (tools/progress.py
    UNNAMED), los limites de la libreria y de ulz_decompress, y los callbacks que corrige
    FixFunctionStarts. Una firma ambigua se resuelve con el desplazamiento de las vecinas;
  - por CORRESPONDENCIA de datos: en las funciones emparejadas por firma, las direcciones de
    datos que usan las mismas instrucciones (lui + desplazamiento, o relativas a $gp) dan parejas EU -> US;
    de ahi el puntero al estado del juego y el estado (check.py) y el puntero al bufer del
    mando (depuracion de native_main);
  - el modo de video: el argumento con que el juego llama a SetVideoMode (0 NTSC, 1 PAL).

Validacion: aplicado al propio ACE.BIN europeo debe dar exactamente las constantes que ya
usaban las herramientas (EU_KNOWN); si no, termina con error. Para la US se comprueba que
ac3_main por el arranque y por firma coinciden y que la libreria tiene un unico
desplazamiento.

Salida: un JSON (lo leen las herramientas de Python) y una cabecera de C (la incluyen
native_main.c y game_main.c con -include). Contiene direcciones del ejecutable del juego:
va a data/ (fuera de git).

Uso:
    python3 tools/recomp/region.py <ace.bin plano> <ace_eu.bin plano> <salida.json> [salida.h] [--disc imagen]
(--disc: anota el ejecutable de arranque del disco, que el APK busca para aceptar la imagen)
    python3 tools/recomp/region.py data/exe/ace_us.bin data/exe/ace_eu.bin data/recomp_us/region.json data/recomp_us/ac3_region.h
"""
import collections
import json
import os
import struct
import sys

BASE = 0x80010000
# constantes europeas que ya usan las herramientas (recomp.py, hle.py, check.py, recomp_rt.c,
# native_main.c, aceovl.py): la referencia contra la que se valida este programa
EU_KNOWN = {
    'start': 0x8002521C, 'main': 0x80025D08, 'gp': 0x800BA9F4, 'bss': [0x800BACB0, 0x800C9DC0],
    'ovl_base': 0x800C9DC0, 'own_start': 0x800252CC, 'lib': [0x80099DE0, 0x800AE09C],
    'ulz': [0x800AE118, 0x800AE528], 'extra_lib': [0x80025214, 0x80025240, 0x800252BC],
    'mode_table': 0x800AE544,
    'ovl_end': {'1': 0x800DB010, '2': 0x800F9E18, '3': 0x800DECE8, '4': 0x800D96C0, '5': 0x800CBA38},
    'g_state_ptr': 0x800BACD0, 'g_state': 0x800BD158, 'pad_buf': 0x800BAA04, 'video_mode': 1,
    'transmatrix': 0x800A53A8,
}
UNNAMED_EU = {0x8009e238: 'SpuQuit', 0x8009eec8: 'SpuSetReverbVoice', 0x800a11f8: 'SetVideoMode',
              0x800a55b8: 'ReadGeomScreen', 0x800a55e8: 'SetGeomOffset', 0x800a5608: 'SetGeomScreen',
              0x800a58e8: 'NormalClip', 0x800aacd8: 'DecDCTReset', 0x800aae30: 'DecDCTin',
              0x800aaeac: 'DecDCTout', 0x800aaf08: 'DecDCToutSync', 0x800aaf74: 'DecDCToutCallback',
              0x800ac328: 'strlen', 0x800a53a8: 'TransMatrix', 0x800ad7f8: 'ExitCriticalSection',
              0x800ad7e8: 'EnterCriticalSection'}
FIX_STARTS_EU = [0x8008BCBC, 0x8008BE4C, 0x8008B914, 0x8008BA18, 0x8008BB1C, 0x8008B1A8]
MEMOPS = {0x09, 0x0D, 0x0F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x32, 0x3A}


def words(d):
    return list(struct.unpack('<%dI' % (len(d) // 4), d[:len(d) // 4 * 4]))


def mask(w):
    op = w >> 26
    if op in (2, 3):
        return w & 0xFC000000
    if op in MEMOPS:
        return w & 0xFFFF0000
    return w


def simm(w):
    i = w & 0xFFFF
    return i - 0x10000 if i & 0x8000 else i


class Matcher:
    """firma: ventana de K instrucciones enmascaradas a partir de la direccion"""
    def __init__(self, eu, other):
        self.ew, self.uw = [mask(w) for w in words(eu)], [mask(w) for w in words(other)]
        self.idx = collections.defaultdict(list)
        for i in range(len(self.uw) - 8):
            self.idx[tuple(self.uw[i:i + 8])].append(i)

    def candidates(self, a):
        i = (a - BASE) // 4
        c = self.idx.get(tuple(self.ew[i:i + 8]), [])
        for k in (16, 32, 64, 128):
            c2 = [j for j in c if self.uw[j:j + k] == self.ew[i:i + k]]
            if not c2:   # la ventana larga ya no casa con ninguna: queda la ambiguedad corta
                break
            c = c2
            if len(c) == 1:
                break
        return [BASE + 4 * j for j in c]

    def map(self, a, near=None):
        c = self.candidates(a)
        if len(c) == 1:
            return c[0]
        if len(c) > 1:   # ambigua (funciones casi iguales): la del desplazamiento de las vecinas
            for d in near or self.neighbour_deltas(a):
                if a + d in c:
                    return a + d
        return None

    def neighbour_deltas(self, a):
        out = []
        for step in range(1, 64):
            for b in (a - 16 * step, a + 16 * step):
                c = self.candidates(b)
                if len(c) == 1 and c[0] - b not in out:
                    out.append(c[0] - b)
            if out:
                return out
        return out


def crt0(d, entry):
    """limites de la BSS, $gp, ac3_main y direcciones de SN del arranque de PsyQ"""
    w = lambda a: struct.unpack_from('<I', d, a - BASE)[0]
    lui, pairs, gp, jals = {}, [], None, []
    for a in range(entry, entry + 0xC0, 4):
        x = w(a)
        op, rs, rt = x >> 26, (x >> 21) & 31, (x >> 16) & 31
        if op == 0x0F:
            lui[rt] = (x & 0xFFFF) << 16
        elif op == 0x09 and rs in lui and rs == rt:
            v = (lui[rs] + simm(x)) & 0xFFFFFFFF
            if rt == 28:
                gp = v
            else:
                pairs.append(v)
        elif op == 3:
            jals.append((a, ((a + 4) & 0xF0000000) | ((x & 0x3FFFFFF) << 2)))
        if x == 0x0000004D:   # break tras ac3_main
            break
    jal_main, main = jals[-1]
    return {'start': entry, 'main': main, 'gp': gp, 'bss': [pairs[0], pairs[1]], 'ovl_base': pairs[1],
            'own_start': jal_main + 0x10,
            'extra_lib': [entry - 8, entry + 0x24, jal_main]}   # __main, stup1, stup0 (Ghidra)


def mode_table(d, ovl_base):
    """(direccion de la tabla de modos, {overlay: fin}) buscando el modo del overlay 1"""
    for off in range(0, len(d) - 16, 4):
        steps, oid, lo, hi = struct.unpack_from('<4I', d, off)
        if oid == 1 and lo == ovl_base and lo < hi < 0x80200000:
            # retroceder hasta el primer modo coherente (como aceovl.read_modes)
            t = off
            while t >= 16:
                s, o, l, h = struct.unpack_from('<4I', d, t - 16)
                if not (0x80000000 <= s < 0x80200000 and 0x80000000 <= l < h < 0x80200000):
                    break
                t -= 16
            ends, m = {}, t
            while m + 16 <= len(d):
                s, o, l, h = struct.unpack_from('<4I', d, m)
                if not (0x80000000 <= s < 0x80200000 and 0x80000000 <= l < h < 0x80200000):
                    break
                if 1 <= o <= 5:
                    ends[str(o)] = h
                m += 16
            return BASE + t, ends
    raise SystemExit('no se encuentra la tabla de modos')


def data_pairs(eu, other, matcher, own_eu, gp_eu, gp_o):
    """{dir EU: dir otra} de datos: misma instruccion (lui + desplazamiento) en funciones
    emparejadas por firma; solo parejas con un unico valor"""
    ew, ow = words(eu), words(other)
    seen = collections.defaultdict(collections.Counter)
    for lo, hi in own_eu:
        a = lo
        while a < hi:
            b = matcher.map(a, near=[])
            if b is None:
                a += 64
                continue
            n = 0
            i, j = (a - BASE) // 4, (b - BASE) // 4
            lu_e, lu_o = {}, {}
            while n < 512 and i + n < len(ew) and j + n < len(ow) and mask(ew[i + n]) == mask(ow[j + n]):
                x, y = ew[i + n], ow[j + n]
                op, rs, rt = x >> 26, (x >> 21) & 31, (x >> 16) & 31
                if op == 0x0F:
                    lu_e[rt], lu_o[rt] = (x & 0xFFFF) << 16, (y & 0xFFFF) << 16
                elif op in MEMOPS and (rs in lu_e or rs == 28):
                    # absoluta (lui + desplazamiento, propagado por addiu) o relativa a $gp
                    be, bo = (gp_eu, gp_o) if rs == 28 else (lu_e[rs], lu_o[rs])
                    ve = (be + simm(x)) & 0xFFFFFFFF
                    vo = (bo + simm(y)) & 0xFFFFFFFF
                    if 0x80000000 <= ve < 0x80200000:
                        seen[ve][vo] += 1
                    if op == 0x09:
                        lu_e[rt], lu_o[rt] = ve, vo
                    elif op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26):
                        lu_e.pop(rt, None)
                elif op == 0:
                    lu_e.pop((x >> 11) & 31, None)
                elif op in (3,) or (op == 0 and x & 63 == 9):
                    for r in list(range(1, 16)) + [24, 25, 31]:
                        lu_e.pop(r, None)
                elif op in MEMOPS or 0x08 <= op <= 0x0E:
                    if op not in (0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x3A):
                        lu_e.pop(rt, None)
                n += 1
            a += 4 * max(n, 16)
    return {e: c.most_common(1)[0][0] for e, c in seen.items() if len(c) == 1}


def video_mode(d, setvideo):
    """argumento (addiu a0, zero, N o move a0, zero) de las llamadas a SetVideoMode"""
    modes = set()
    for off in range(0, len(d) - 8, 4):
        x = struct.unpack_from('<I', d, off)[0]
        a = BASE + off
        if x >> 26 == 3 and ((a + 4) & 0xF0000000) | ((x & 0x3FFFFFF) << 2) == setvideo:
            for k in (4, -4, -8, -12):   # hueco de retardo o justo antes
                y = struct.unpack_from('<I', d, off + k)[0]
                if y >> 16 == 0x2404:          # addiu a0, zero, N
                    modes.add(y & 0xFFFF)
                    break
                if y == 0x00002021:            # addu a0, zero, zero (move a0, 0)
                    modes.add(0)
                    break
    return modes


def profile(path, eu_path):
    d, eu = open(path, 'rb').read(), open(eu_path, 'rb').read()
    entry = None
    exe = os.path.splitext(path)[0] + '.exe'
    if os.path.exists(exe):
        entry = struct.unpack_from('<I', open(exe, 'rb').read(), 0x10)[0]
    if entry is None:   # sin el PS-X EXE: el arranque de PsyQ es el primer "lui v0; addiu v0;
        raise SystemExit('falta %s (make acebin) para leer la entrada' % exe)
    p = crt0(d, entry)
    p['mode_table'], p['ovl_end'] = mode_table(d, p['ovl_base'])
    m = Matcher(eu, d)
    lib_lo, lib_hi = m.map(EU_KNOWN['lib'][0]), m.map(EU_KNOWN['lib'][1] - 4)
    p['lib'] = [lib_lo, lib_hi + 4]
    lib_delta = lib_lo - EU_KNOWN['lib'][0]
    if lib_hi + 4 - EU_KNOWN['lib'][1] != lib_delta:
        raise SystemExit('la libreria no tiene un desplazamiento unico')
    p['lib_delta'] = lib_delta
    u0, u1 = m.map(EU_KNOWN['ulz'][0]), m.map(EU_KNOWN['ulz'][1] - 4)
    p['ulz'] = [u0, u1 + 4]
    p['unnamed'] = {}
    for a, n in sorted(UNNAMED_EU.items()):
        b = m.map(a)
        if b is None or b - a != lib_delta:
            raise SystemExit('firma de %s (%08x) no encontrada con el desplazamiento de la libreria' % (n, a))
        p['unnamed']['%08x' % b] = n
    p['transmatrix'] = m.map(EU_KNOWN['transmatrix'])
    p['fix_starts'] = []
    for a in FIX_STARTS_EU:
        b = m.map(a)
        if b is None:
            raise SystemExit('callback %08x sin pareja' % a)
        p['fix_starts'].append(b)
    main_sig = m.map(EU_KNOWN['main'])
    if main_sig != p['main']:
        raise SystemExit('ac3_main: el arranque dice %08x y la firma %08x' % (p['main'], main_sig or 0))
    own_eu = [(EU_KNOWN['own_start'], EU_KNOWN['lib'][0])]
    pairs = data_pairs(eu, d, m, own_eu, EU_KNOWN['gp'], p['gp'])
    for k in ('g_state_ptr', 'g_state', 'pad_buf'):
        v = pairs.get(EU_KNOWN[k])
        if v is None:
            raise SystemExit('%s (%08x): sin correspondencia de datos' % (k, EU_KNOWN[k]))
        p[k] = v
    p['data_pairs'] = len(pairs)
    vm = video_mode(d, m.map(0x800A11F8))
    if len(vm) != 1:
        raise SystemExit('modo de video ambiguo: %s' % vm)
    p['video_mode'] = vm.pop()
    p['refresh'] = 50 if p['video_mode'] == 1 else 60
    p['stem'] = os.path.splitext(os.path.basename(path))[0]
    return p


def check_eu(p):
    bad = [k for k, v in EU_KNOWN.items() if p.get(k) != v]
    if bad:
        raise SystemExit('VALIDACION EU FALLIDA en %s: %s' % (bad, {k: p.get(k) for k in bad}))
    want = {'%08x' % (a + 0) for a in UNNAMED_EU}
    if set(p['unnamed']) != want or p['fix_starts'] != FIX_STARTS_EU:
        raise SystemExit('VALIDACION EU FALLIDA en unnamed/fix_starts')


def header(p):
    h = lambda v: '0x%08Xu' % v
    lines = ['/* GENERADO por tools/recomp/region.py desde %s: direcciones del ejecutable del juego,' % p['stem'],
             ' * no entra en git. Se incluye con -include al compilar native_main.c / game_main.c. */',
             '#define AC3_REGION_%s 1' % p['stem'].split('_')[-1].upper(),
             '#define AC3_MAIN %s' % h(p['main']),
             '#define AC3_GP %s' % h(p['gp']),
             '#define AC3_BSS_START %s' % h(p['bss'][0]),
             '#define AC3_BSS_END %s' % h(p['bss'][1]),
             '#define AC3_OVL_BASE %s' % h(p['ovl_base']),
             '#define AC3_LIB_LO %s' % h(p['lib'][0]),
             '#define AC3_LIB_HI %s' % h(p['lib'][1]),
             '#define AC3_LIB_MAIN %s' % h(p['extra_lib'][0]),
             '#define AC3_REFRESH %d' % p['refresh'],
             '#define AC3_PAD_BUF %s' % h(p['pad_buf']),
             '#define AC3_RC_FUNCS rc_funcs_%s' % p['stem'],
             '#define AC3_RC_NFUNCS rc_nfuncs_%s' % p['stem']]
    if 'boot_exe' in p:   # el APK comprueba que el disco elegido es de esta version
        lines.append('#define AC3_BOOT_EXE "%s"' % p['boot_exe'].replace('\\', '\\\\'))
    return '\n'.join(lines) + '\n'


def boot_exe(image):
    """ejecutable de arranque del disco (linea BOOT de SYSTEM.CNF), como lo busca CdSearchFile"""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    from psxiso import Disc
    cnf = Disc(image).read_file('SYSTEM.CNF').decode('latin1')
    for line in cnf.splitlines():
        k, _, v = line.partition('=')
        if k.strip().upper() == 'BOOT':
            name = v.strip().split(':', 1)[-1].lstrip('\\').split()[0]
            return '\\' + name if ';' in name else '\\' + name + ';1'
    raise SystemExit('SYSTEM.CNF sin BOOT')


def main():
    disc = None
    if '--disc' in sys.argv:
        i = sys.argv.index('--disc')
        disc = sys.argv[i + 1]
        del sys.argv[i:i + 2]
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    eu = sys.argv[2]
    pe = profile(eu, eu)
    check_eu(pe)
    print('validacion: el perfil del ACE.BIN europeo coincide con las %d constantes conocidas' % len(EU_KNOWN))
    p = profile(sys.argv[1], eu)
    if os.path.abspath(sys.argv[1]) == os.path.abspath(eu):
        check_eu(p)
    if disc:
        p['boot_exe'] = boot_exe(disc)
    os.makedirs(os.path.dirname(os.path.abspath(sys.argv[3])), exist_ok=True)
    json.dump(p, open(sys.argv[3], 'w'), indent=1)
    if len(sys.argv) > 4:
        open(sys.argv[4], 'w').write(header(p))
    print('%s: main %08x, gp %08x, BSS %08x-%08x, overlays en %08x, libreria %08x-%08x (desplazamiento %+#x), '
          'modo de video %d (%d Hz), %d parejas de datos' % (p['stem'], p['main'], p['gp'], p['bss'][0], p['bss'][1],
                                                            p['ovl_base'], p['lib'][0], p['lib'][1], p['lib_delta'],
                                                            p['video_mode'], p['refresh'], p['data_pairs']))
    print('overlays: ' + ', '.join('%s hasta %08x' % (k, v) for k, v in sorted(p['ovl_end'].items())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
