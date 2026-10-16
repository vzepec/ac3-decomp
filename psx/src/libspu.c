/* libspu sobre la SPU por software (ver psx_libspu.h). Comportamiento leido del
 * ensamblador de la libreria original (ACE.BIN europeo, 0x8009D548-0x800A0438). */
#include <math.h>
#include <string.h>

#include "psx_libspu.h"

static PsxSpu g_spu;
static struct {
    uint32_t keys;              /* voces con la tecla pulsada (0x800B25D0 en la original) */
    uint16_t sample_note[24];   /* 0x800B2600 */
    uint32_t xfer_addr;         /* 0x800B265C: direccion de transferencia >> 3 */
    int32_t xfer_mode;
    int32_t reverb_on, reverb_mode;
    int16_t depth_l, depth_r;
    int started;
    uint16_t t12[12], t128[128];
} L;

PsxSpu *psx_spu(void) { return &g_spu; }

static void wr(uint32_t off, uint16_t v) { psx_spu_write16(&g_spu, off, v); }
static uint16_t rd(uint32_t off) { return psx_spu_read16(&g_spu, off); }

/* _spu_FsetRXXa con el modo de memoria de _spu_init: alineado a 8, desplazamiento 3 */
static uint32_t addr_reg(uint32_t a) {
    if (a % 8) a = (a + 8) & ~7u;
    return a >> 3;
}

static void tables(void) {
    /* tablas de _spu_note2pitch (0x800B2AB4, 0x800B2ACC): floor(0x8000 * 2^(i/12)) y
     * floor(0x8000 * 2^(j/1536)); comprobado contra el ACE.BIN en las 140 entradas */
    for (int i = 0; i < 12; i++) L.t12[i] = (uint16_t)floor(32768.0 * pow(2.0, i / 12.0));
    for (int j = 0; j < 128; j++) L.t128[j] = (uint16_t)floor(32768.0 * pow(2.0, j / 1536.0));
}

void SpuInit(void) {
    psx_spu_reset(&g_spu);
    memset(&L, 0, sizeof L);
    tables();
    /* _spu_init: volumenes a 0, transferencia normal, todas las teclas soltadas */
    wr(0x180, 0); wr(0x182, 0); wr(0x1AA, 0);
    wr(0x1AC, 4);
    wr(0x184, 0); wr(0x186, 0);
    wr(0x18C, 0xFFFF); wr(0x18E, 0xFFFF);
    wr(0x198, 0); wr(0x19A, 0);
    wr(0x190, 0); wr(0x192, 0); wr(0x194, 0); wr(0x196, 0);
    wr(0x1B0, 0); wr(0x1B2, 0); wr(0x1B4, 0); wr(0x1B6, 0);
    for (int i = 0; i < 24; i++) {
        wr(i * 16 + 0, 0); wr(i * 16 + 2, 0); wr(i * 16 + 4, 0x3FFF); wr(i * 16 + 6, 0x200);
        wr(i * 16 + 8, 0); wr(i * 16 + 10, 0);
        L.sample_note[i] = 0xC000;
    }
    wr(0x1AA, 0xC000);   /* SPU encendida y sin silencio */
    L.xfer_addr = 0x200;
    SpuStart();
}

void SpuStart(void) { L.started = 1; }
void SpuQuit(void) { L.started = 0; }

static uint16_t volmode_bits(int16_t mode) {
    unsigned m = (uint16_t)(mode - 1);
    return m < 7 ? (uint16_t)(0x8000 + 0x1000 * m) : 0;
}

/* volumen con modo: valor & 0x7FFF; si hay modo de barrido, recortado a 0..127 */
static uint16_t vol_with_mode(int16_t v, uint16_t mode_bits) {
    uint16_t x = (uint16_t)v & 0x7FFF;
    if (mode_bits) x = v > 127 ? 127 : v < 0 ? 0 : (uint16_t)v;
    return x | mode_bits;
}

void SpuSetCommonAttr(const SpuCommonAttr *a) {
    uint32_t m = a->mask;
    int all = m == 0;
    if (all || (m & 1)) wr(0x180, vol_with_mode(a->mvol.left, (all || (m & 4)) ? volmode_bits(a->mvolmode.left) : 0));
    if (all || (m & 2)) wr(0x182, vol_with_mode(a->mvol.right, (all || (m & 8)) ? volmode_bits(a->mvolmode.right) : 0));
    if (all || (m & 0x40)) wr(0x1B0, (uint16_t)a->cd.volume.left);
    if (all || (m & 0x80)) wr(0x1B2, (uint16_t)a->cd.volume.right);
    if (all || (m & 0x400)) wr(0x1B4, (uint16_t)a->ext.volume.left);
    if (all || (m & 0x800)) wr(0x1B6, (uint16_t)a->ext.volume.right);
    if (all || (m & 0x100)) wr(0x1AA, a->cd.reverb ? rd(0x1AA) | 4 : rd(0x1AA) & ~4);
    if (all || (m & 0x200)) wr(0x1AA, a->cd.mix ? rd(0x1AA) | 1 : rd(0x1AA) & ~1);
    if (all || (m & 0x1000)) wr(0x1AA, a->ext.reverb ? rd(0x1AA) | 8 : rd(0x1AA) & ~8);
    if (all || (m & 0x2000)) wr(0x1AA, a->ext.mix ? rd(0x1AA) | 2 : rd(0x1AA) & ~2);
}

uint16_t _spu_note2pitch(int cen_hi, int cen_lo, int note_hi, int note_lo) {
    if (!L.t12[0]) tables();
    unsigned fine = (unsigned)(note_lo + cen_lo) & 0xFFFF;
    int n = (int16_t)(note_hi + (int)(fine >> 7) - cen_hi);
    int q = n / 12, r = n - q * 12, oct = q - 2;
    if ((int16_t)r < 0) { r += 12; oct = q - 3; }
    int32_t p = (int32_t)((uint32_t)L.t12[(int16_t)r] * L.t128[fine & 0x7F]) >> 16;
    if ((int16_t)oct >= 0) return 0x3FFF;
    int sh = -(int16_t)oct;
    return (uint16_t)(((uint32_t)p + (1u << (sh - 1))) >> sh);
}

void SpuSetVoiceAttr(const SpuVoiceAttr *a) {
    uint32_t m = a->mask;
    int all = m == 0;
    for (int i = 0; i < 24; i++) {
        if (!(a->voice & (1u << i))) continue;
        uint32_t b = i * 16;
        if (all || (m & 0x10)) wr(b + 4, a->pitch);
        if (all || (m & 0x40)) L.sample_note[i] = a->sample_note;
        if (all || (m & 0x20))
            wr(b + 4, _spu_note2pitch(L.sample_note[i] >> 8, L.sample_note[i] & 0xFF, a->note >> 8, a->note & 0xFF));
        if (all || (m & 1)) wr(b + 0, vol_with_mode(a->volume.left, (all || (m & 4)) ? volmode_bits(a->volmode.left) : 0));
        if (all || (m & 2)) wr(b + 2, vol_with_mode(a->volume.right, (all || (m & 8)) ? volmode_bits(a->volmode.right) : 0));
        if (all || (m & 0x80)) wr(b + 6, (uint16_t)addr_reg(a->addr));
        if (all || (m & 0x10000)) wr(b + 14, (uint16_t)addr_reg(a->loop_addr));
        if (all || (m & 0x20000)) wr(b + 8, a->adsr1);
        if (all || (m & 0x40000)) wr(b + 10, a->adsr2);
        if (all || (m & 0x800)) {
            unsigned ar = a->ar > 127 ? 127 : a->ar;
            unsigned exp = ((all || (m & 0x100)) && a->a_mode == 5) ? 0x80 : 0;
            wr(b + 8, (uint16_t)((rd(b + 8) & 0xFF) | ((ar | exp) << 8)));
        }
        if (all || (m & 0x1000)) {
            unsigned dr = a->dr > 15 ? 15 : a->dr;
            wr(b + 8, (uint16_t)((rd(b + 8) & 0xFF0F) | (dr << 4)));
        }
        if (all || (m & 0x2000)) {
            unsigned sr = a->sr > 127 ? 127 : a->sr, mode = 0x100;
            if (all || (m & 0x200)) {
                if (a->s_mode == 5) mode = 0x200;
                else if (a->s_mode == 1) mode = 0;
                else if (a->s_mode == 7) mode = 0x300;
            }
            wr(b + 10, (uint16_t)((rd(b + 10) & 0x3F) | ((sr | mode) << 6)));
        }
        if (all || (m & 0x4000)) {
            unsigned rr = a->rr > 31 ? 31 : a->rr;
            unsigned mode = ((all || (m & 0x400)) && a->r_mode == 7) ? 0x20 : 0;
            wr(b + 10, (uint16_t)((rd(b + 10) & 0xFFC0) | rr | mode));
        }
        if (all || (m & 0x8000)) {
            unsigned sl = a->sl > 15 ? 15 : a->sl;
            wr(b + 8, (uint16_t)((rd(b + 8) & 0xFFF0) | sl));
        }
    }
}

void SpuSetKey(int32_t on_off, uint32_t voice_bit) {
    uint32_t v = voice_bit & 0xFFFFFF;
    if (on_off == 0) {
        wr(0x18C, (uint16_t)v); wr(0x18E, (uint16_t)(v >> 16));
        L.keys &= ~v;
    } else if (on_off == 1) {
        wr(0x188, (uint16_t)v); wr(0x18A, (uint16_t)(v >> 16));
        L.keys |= v;
    }
}

/* 0 apagada, 1 tecla y envolvente, 2 solo envolvente (liberacion), 3 tecla sin envolvente */
void SpuGetAllKeysStatus(char *status) {
    for (int i = 0; i < 24; i++) {
        int env = rd(i * 16 + 12) != 0;
        status[i] = (char)((L.keys & (1u << i)) ? (env ? 1 : 3) : (env ? 2 : 0));
    }
}

int32_t SpuSetTransferMode(int32_t mode) { L.xfer_mode = mode == 1 ? 1 : 0; return L.xfer_mode; }

uint32_t SpuSetTransferStartAddr(uint32_t addr) {
    if (addr - 0x1010 >= 0x7EFE9) return 0;
    L.xfer_addr = addr_reg(addr);
    return L.xfer_addr << 3;
}

uint32_t SpuWrite(const uint8_t *addr, uint32_t size) {
    if (size > 0x7EFF0) size = 0x7EFF0;
    wr(0x1A6, (uint16_t)L.xfer_addr);
    psx_spu_dma_write(&g_spu, addr, (size + 3) / 4);
    return size;
}

uint32_t SpuWrite0(uint32_t size) {
    static const uint8_t zero[1024];
    wr(0x1A6, (uint16_t)L.xfer_addr);
    for (uint32_t done = 0; done < size; done += sizeof zero) {
        uint32_t n = size - done < sizeof zero ? size - done : sizeof zero;
        psx_spu_dma_write(&g_spu, zero, (n + 3) / 4);
    }
    return size;
}

uint32_t SpuRead(uint8_t *addr, uint32_t size) {
    if (size > 0x7EFF0) size = 0x7EFF0;
    wr(0x1A6, (uint16_t)L.xfer_addr);
    psx_spu_dma_read(&g_spu, addr, (size + 3) / 4);
    return size;
}

int32_t SpuIsTransferCompleted(int32_t flag) { return 1; }

int32_t SpuSetReverb(int32_t on_off) {
    if (on_off == 0) { L.reverb_on = 0; wr(0x1AA, rd(0x1AA) & ~0x80); return 0; }
    if (on_off != 1) return L.reverb_on;
    L.reverb_on = 1;
    wr(0x1AA, rd(0x1AA) | 0x80);
    return 1;
}

int32_t SpuSetReverbDepth(const SpuReverbAttr *a) {
    if (!a->mask || (a->mask & 2)) { wr(0x184, (uint16_t)a->depth.left); L.depth_l = a->depth.left; }
    if (!a->mask || (a->mask & 4)) { wr(0x186, (uint16_t)a->depth.right); L.depth_r = a->depth.right; }
    return 0;
}

uint32_t SpuSetReverbVoice(int32_t on_off, uint32_t voice_bit) {
    uint16_t lo = (uint16_t)voice_bit, hi = (uint16_t)((voice_bit >> 16) & 0xFF);
    if (on_off == 1) { wr(0x198, rd(0x198) | lo); wr(0x19A, rd(0x19A) | hi); }
    else if (on_off == 8) { wr(0x198, lo); wr(0x19A, hi); }
    else if (on_off == 0) { wr(0x198, rd(0x198) & ~lo); wr(0x19A, rd(0x19A) & ~hi); }
    return ((uint32_t)rd(0x19A) << 16 | rd(0x198)) & 0xFFFFFF;
}

int32_t SpuSetReverbModeParam(const SpuReverbAttr *a) {
    if (!a->mask || (a->mask & 1)) L.reverb_mode = a->mode;
    return 0;
}

int32_t SpuClearReverbWorkArea(int32_t mode) { return 0; }
