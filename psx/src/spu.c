/* SPU por software (ver psx_spu.h). */
#include <string.h>

#include "psx_spu.h"

static const int FILT[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

#define R(off) s->regs[(off) >> 1]
enum { MVOL_L = 0x180, MVOL_R = 0x182, KON = 0x188, KOFF = 0x18C, ENDX = 0x19C, XADDR = 0x1A6, FIFO = 0x1A8,
       SPUCNT = 0x1AA, SPUSTAT = 0x1AE, CDVOL_L = 0x1B0, CDVOL_R = 0x1B2, CURVOL_L = 0x1B8, CURVOL_R = 0x1BA };

void psx_spu_reset(PsxSpu *s) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 24; i++) s->v[i].phase = 4;
}

static void decode_block(PsxSpu *s, PsxSpuVoice *v) {
    const uint8_t *b = s->ram + (v->addr & (PSX_SPU_RAM - 1) & ~15u);
    int shift = b[0] & 15, f = (b[0] >> 4) & 7;
    if (shift > 12) shift = 9;   /* psx-spx: 13-15 se comportan como 9 */
    if (f > 4) f = 4;
    v->flags = b[1];
    if ((v->flags & 4) && !v->ignore_loop) v->repeat = v->addr;
    for (int i = 0; i < 28; i++) {
        int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
        int32_t x = (int32_t)((uint32_t)nib << 28) >> 16;   /* nibble con signo << 12 */
        x = (x >> shift) + ((v->s1 * FILT[f][0] + v->s2 * FILT[f][1] + 32) >> 6);
        if (x > 32767) x = 32767;
        if (x < -32768) x = -32768;
        v->buf[i] = (int16_t)x;
        v->s2 = v->s1;
        v->s1 = (int16_t)x;
    }
}

static void key_on(PsxSpu *s, int i) {
    PsxSpuVoice *v = &s->v[i];
    v->addr = (uint32_t)s->regs[i * 8 + 3] * 8;
    v->ignore_loop = 0;
    v->counter = 0;
    v->s1 = v->s2 = v->prev = 0;
    v->phase = 0;
    v->level = 0;
    v->wait = 0;
    s->endx &= ~(1u << i);
    decode_block(s, v);
}

static void key_off(PsxSpu *s, int i) {
    if (s->v[i].phase < 3) { s->v[i].phase = 3; s->v[i].wait = 0; }
}

void psx_spu_write16(PsxSpu *s, uint32_t off, uint16_t val) {
    off &= 0x3FE;
    if (off < 0x180 && (off & 15) == 0xE) {   /* direccion de repeticion */
        int i = off >> 4;
        s->v[i].repeat = (uint32_t)val * 8;
        s->v[i].ignore_loop = 1;
    }
    if (off < 0x180 && (off & 15) == 0xC) s->v[off >> 4].level = (int16_t)val;   /* ENVX */
    switch (off) {
    case KON: case KON + 2: {
        uint32_t bits = (uint32_t)val << (off == KON ? 0 : 16);
        for (int i = 0; i < 24; i++) if (bits & (1u << i)) key_on(s, i);
        break;
    }
    case KOFF: case KOFF + 2: {
        uint32_t bits = (uint32_t)val << (off == KOFF ? 0 : 16);
        for (int i = 0; i < 24; i++) if (bits & (1u << i)) key_off(s, i);
        break;
    }
    case XADDR: s->xfer = (uint32_t)val * 8; break;
    case FIFO:
        s->ram[s->xfer & (PSX_SPU_RAM - 1)] = (uint8_t)val;
        s->ram[(s->xfer + 1) & (PSX_SPU_RAM - 1)] = (uint8_t)(val >> 8);
        s->xfer = (s->xfer + 2) & (PSX_SPU_RAM - 1);
        return;
    case ENDX: case ENDX + 2: case SPUSTAT: return;   /* solo lectura */
    default: break;
    }
    s->regs[off >> 1] = val;
}

uint16_t psx_spu_read16(PsxSpu *s, uint32_t off) {
    off &= 0x3FE;
    if (off < 0x180 && (off & 15) == 0xC) return (uint16_t)s->v[off >> 4].level;
    switch (off) {
    case ENDX: return (uint16_t)s->endx;
    case ENDX + 2: return (uint16_t)(s->endx >> 16);
    case SPUSTAT: /* modo actual = SPUCNT bits 0-5; nunca ocupada; bit 11 = mitad de captura */
        return (uint16_t)((R(SPUCNT) & 0x3F) | (s->cap >= 0x100 ? 0x800 : 0));
    case CURVOL_L: return R(MVOL_L);
    case CURVOL_R: return R(MVOL_R);
    default: return s->regs[off >> 1];
    }
}

void psx_spu_dma_write(PsxSpu *s, const void *src, uint32_t words) {
    const uint8_t *p = src;
    for (uint32_t i = 0; i < words * 4; i++) s->ram[(s->xfer + i) & (PSX_SPU_RAM - 1)] = p[i];
    s->xfer = (s->xfer + words * 4) & (PSX_SPU_RAM - 1);
}

void psx_spu_dma_read(PsxSpu *s, void *dst, uint32_t words) {
    uint8_t *p = dst;
    for (uint32_t i = 0; i < words * 4; i++) p[i] = s->ram[(s->xfer + i) & (PSX_SPU_RAM - 1)];
    s->xfer = (s->xfer + words * 4) & (PSX_SPU_RAM - 1);
}

void psx_spu_cd_push(PsxSpu *s, const int16_t *st, int frames) {
    for (int i = 0; i < frames; i++) {
        if (s->cd_w - s->cd_r >= PSX_SPU_CD_BUF) break;
        s->cd[s->cd_w % PSX_SPU_CD_BUF][0] = st[2 * i];
        s->cd[s->cd_w % PSX_SPU_CD_BUF][1] = st[2 * i + 1];
        s->cd_w++;
    }
}

/* un paso de envolvente segun psx-spx: shift, step, modo exponencial, direccion */
static void env_step(PsxSpuVoice *v, int shift, int step, int exp, int dec) {
    if (v->wait > 0 && --v->wait > 0) return;
    int cycles = 1 << (shift > 11 ? shift - 11 : 0);
    int32_t st = step << (shift < 11 ? 11 - shift : 0);
    if (exp && !dec && v->level > 0x6000) cycles *= 4;
    if (exp && dec) st = st * v->level >> 15;
    v->level += st;
    if (v->level < 0) v->level = 0;
    if (v->level > 0x7FFF) v->level = 0x7FFF;
    v->wait = cycles;
}

static void envelope(PsxSpu *s, int i) {
    PsxSpuVoice *v = &s->v[i];
    uint16_t a1 = s->regs[i * 8 + 4], a2 = s->regs[i * 8 + 5];
    switch (v->phase) {
    case 0: {   /* ataque: AR (7 bits), bit 15 exponencial */
        int ar = (a1 >> 8) & 0x7F;
        env_step(v, ar >> 2, 7 - (ar & 3), a1 >> 15, 0);
        if (v->level >= 0x7FFF) { v->phase = 1; v->wait = 0; }
        break;
    }
    case 1: {   /* caida: DR = shift, paso -8, exponencial, hasta (SL+1)*0x800 */
        if (v->level <= (int32_t)(((a1 & 15) + 1) * 0x800)) { v->phase = 2; v->wait = 0; break; }
        env_step(v, (a1 >> 4) & 15, -8, 1, 1);
        break;
    }
    case 2: {   /* sostenido: SR (7 bits), bit 14 baja, bit 15 exponencial */
        int sr = (a2 >> 6) & 0x7F, dec = (a2 >> 14) & 1;
        env_step(v, sr >> 2, dec ? -8 + (sr & 3) : 7 - (sr & 3), a2 >> 15, dec);
        break;
    }
    case 3:     /* liberacion: RR = shift, paso -8, bit 5 exponencial */
        env_step(v, a2 & 31, -8, (a2 >> 5) & 1, 1);
        if (v->level <= 0) { v->level = 0; v->phase = 4; }
        break;
    default: break;
    }
}

static int32_t vol(uint16_t r) {
    if (r & 0x8000) return (int16_t)((r & 0x7F) << 8);   /* barrido: SIN HACER */
    return (int16_t)(r << 1);
}

static int16_t clamp16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

void psx_spu_render(PsxSpu *s, int16_t *out, int frames) {
    uint16_t cnt = R(SPUCNT);
    for (int n = 0; n < frames; n++) {
        int32_t l = 0, r = 0, cap1 = 0, cap3 = 0, cdl = 0, cdr = 0;
        for (int i = 0; i < 24; i++) {
            PsxSpuVoice *v = &s->v[i];
            if (v->phase == 4) continue;
            int idx = (int)(v->counter >> 12), frac = (int)(v->counter & 0xFFF);
            int16_t cur = v->buf[idx], prev = idx ? v->buf[idx - 1] : v->prev;
            int32_t smp = prev + (((int32_t)cur - prev) * frac >> 12);
            envelope(s, i);
            smp = smp * v->level >> 15;
            if (i == 1) cap1 = smp;   /* captura: voces 1 y 3 tras la envolvente, antes del volumen */
            if (i == 3) cap3 = smp;
            if (!(s->mute_out >> i & 1)) {
                int32_t o = s->out_quiet[i] ? smp * (4096 - s->out_quiet[i]) >> 12 : smp;
                l += o * vol(s->regs[i * 8]) >> 15;
                r += o * vol(s->regs[i * 8 + 1]) >> 15;
            }
            uint32_t pitch = s->regs[i * 8 + 2];
            if (pitch > 0x4000) pitch = 0x4000;
            v->counter += pitch;
            while (v->counter >= 28u << 12) {   /* fin del bloque: banderas y siguiente */
                v->counter -= 28u << 12;
                v->prev = v->buf[27];
                if (v->flags & 1) {
                    s->endx |= 1u << i;
                    v->addr = v->repeat;
                    if (!(v->flags & 2)) { v->phase = 4; v->level = 0; }
                } else {
                    v->addr += 16;
                }
                decode_block(s, v);
            }
        }
        if ((cnt & 1) && s->cd_r != s->cd_w) {   /* entrada de CD */
            cdl = s->cd[s->cd_r % PSX_SPU_CD_BUF][0] * (int16_t)R(CDVOL_L) >> 15;
            cdr = s->cd[s->cd_r % PSX_SPU_CD_BUF][1] * (int16_t)R(CDVOL_R) >> 15;
            l += cdl;
            r += cdr;
            s->cd_r++;
        } else if (s->cd_r != s->cd_w) {
            s->cd_r++;
        }
        {   /* captura en anillo de 0x200 muestras */
            int16_t cv[4] = {clamp16(cdl), clamp16(cdr), clamp16(cap1), clamp16(cap3)};
            for (int k = 0; k < 4; k++) {
                s->ram[k * 0x400 + s->cap * 2] = (uint8_t)cv[k];
                s->ram[k * 0x400 + s->cap * 2 + 1] = (uint8_t)(cv[k] >> 8);
            }
            s->cap = (s->cap + 1) & 0x1FF;
        }
        if (!(cnt & 0x8000) || !(cnt & 0x4000)) { l = r = 0; }   /* SPU apagada o en silencio */
        l = clamp16(l) * vol(R(MVOL_L)) >> 15;
        r = clamp16(r) * vol(R(MVOL_R)) >> 15;
        out[2 * n] = clamp16(l);
        out[2 * n + 1] = clamp16(r);
        s->samples++;
    }
}

size_t psx_spu_sizeof(void) { return sizeof(PsxSpu); }
