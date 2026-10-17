/* libpress (etapa 6, paso 1): descompresion de los fotogramas de video (ver psx_libpress.h).
 * DecDCTvlc2 convierte el flujo de bits (versiones 2 y 3) en los codigos run-level que
 * consume el MDEC; se compara con la original en tools/psyqcheck_press.py. */
#include <string.h>

#include "psx_libpress.h"
#include "psx_mem.h"

/* tabla de coeficientes AC de MPEG-1 (B.14, "siguiente coeficiente"): codigo sin el bit
 * de signo, longitud, run, nivel */
static const struct { uint16_t code; uint8_t len, run, level; } AC[] = {
    {0x3, 2, 0, 1}, {0x3, 3, 1, 1}, {0x4, 4, 0, 2}, {0x5, 4, 2, 1}, {0x5, 5, 0, 3}, {0x7, 5, 3, 1},
    {0x6, 5, 4, 1}, {0x6, 6, 1, 2}, {0x7, 6, 5, 1}, {0x5, 6, 6, 1}, {0x4, 6, 7, 1}, {0x6, 7, 0, 4},
    {0x4, 7, 2, 2}, {0x7, 7, 8, 1}, {0x5, 7, 9, 1}, {0x26, 8, 0, 5}, {0x21, 8, 0, 6}, {0x25, 8, 1, 3},
    {0x24, 8, 3, 2}, {0x27, 8, 10, 1}, {0x23, 8, 11, 1}, {0x22, 8, 12, 1}, {0x20, 8, 13, 1},
    {0xA, 10, 0, 7}, {0xC, 10, 1, 4}, {0xB, 10, 2, 3}, {0xF, 10, 4, 2}, {0x9, 10, 5, 2}, {0xE, 10, 14, 1},
    {0xD, 10, 15, 1}, {0x8, 10, 16, 1}, {0x1D, 12, 0, 8}, {0x18, 12, 0, 9}, {0x13, 12, 0, 10},
    {0x10, 12, 0, 11}, {0x1B, 12, 1, 5}, {0x14, 12, 2, 4}, {0x1C, 12, 3, 3}, {0x12, 12, 4, 3},
    {0x1E, 12, 6, 2}, {0x15, 12, 7, 2}, {0x11, 12, 8, 2}, {0x1F, 12, 17, 1}, {0x1A, 12, 18, 1},
    {0x19, 12, 19, 1}, {0x17, 12, 20, 1}, {0x16, 12, 21, 1}, {0x1A, 13, 0, 12}, {0x19, 13, 0, 13},
    {0x18, 13, 0, 14}, {0x17, 13, 0, 15}, {0x16, 13, 1, 6}, {0x15, 13, 1, 7}, {0x14, 13, 2, 5},
    {0x13, 13, 3, 4}, {0x12, 13, 5, 3}, {0x11, 13, 9, 2}, {0x10, 13, 10, 2}, {0x1F, 13, 22, 1},
    {0x1E, 13, 23, 1}, {0x1D, 13, 24, 1}, {0x1C, 13, 25, 1}, {0x1B, 13, 26, 1}, {0x1F, 14, 0, 16},
    {0x1E, 14, 0, 17}, {0x1D, 14, 0, 18}, {0x1C, 14, 0, 19}, {0x1B, 14, 0, 20}, {0x1A, 14, 0, 21},
    {0x19, 14, 0, 22}, {0x18, 14, 0, 23}, {0x17, 14, 0, 24}, {0x16, 14, 0, 25}, {0x15, 14, 0, 26},
    {0x14, 14, 0, 27}, {0x13, 14, 0, 28}, {0x12, 14, 0, 29}, {0x11, 14, 0, 30}, {0x10, 14, 0, 31},
    {0x18, 15, 0, 32}, {0x17, 15, 0, 33}, {0x16, 15, 0, 34}, {0x15, 15, 0, 35}, {0x14, 15, 0, 36},
    {0x13, 15, 0, 37}, {0x12, 15, 0, 38}, {0x11, 15, 0, 39}, {0x10, 15, 0, 40}, {0x1F, 15, 1, 8},
    {0x1E, 15, 1, 9}, {0x1D, 15, 1, 10}, {0x1C, 15, 1, 11}, {0x1B, 15, 1, 12}, {0x1A, 15, 1, 13},
    {0x19, 15, 1, 14}, {0x13, 16, 1, 15}, {0x12, 16, 1, 16}, {0x11, 16, 1, 17}, {0x10, 16, 1, 18},
    {0x14, 16, 6, 3}, {0x1A, 16, 11, 2}, {0x19, 16, 12, 2}, {0x18, 16, 13, 2}, {0x17, 16, 14, 2},
    {0x16, 16, 15, 2}, {0x15, 16, 16, 2}, {0x1F, 16, 27, 1}, {0x1E, 16, 28, 1}, {0x1D, 16, 29, 1},
    {0x1C, 16, 30, 1}, {0x1B, 16, 31, 1},
};
/* tamano del DC en la version 3 (MPEG-1 dct_dc_size): codigo, longitud */
static const struct { uint8_t code, len; } DC_Y[9] = {{4, 3}, {0, 2}, {1, 2}, {5, 3}, {6, 3}, {14, 4}, {30, 5}, {62, 6}, {126, 7}};
static const struct { uint8_t code, len; } DC_C[9] = {{0, 2}, {1, 2}, {2, 2}, {6, 3}, {14, 4}, {30, 5}, {62, 6}, {126, 7}, {254, 8}};

static int32_t g_max;   /* DecDCTvlcSize2: palabras de salida por llamada (0 = sin limite) */

typedef struct { const uint8_t *p; uint32_t bits; int n; } Bits;

static uint32_t peek(Bits *b, int k) {
    while (b->n < k) {   /* palabras de 16 bits en little-endian, bits de mayor a menor */
        b->bits = (b->bits << 16) | (uint32_t)(b->p[0] | b->p[1] << 8);
        b->p += 2;
        b->n += 16;
    }
    return (b->bits >> (b->n - k)) & ((1u << k) - 1);
}
static void skip(Bits *b, int k) { b->n -= k; }
static uint32_t get(Bits *b, int k) { uint32_t v = peek(b, k); skip(b, k); return v; }

void DecDCTvlcBuild(uint16_t *table) { (void)table; }   /* la tabla de la original no hace falta */
int32_t DecDCTvlcSize2(int32_t size) { int32_t o = g_max; g_max = size; return o; }

int DecDCTvlc2(uint32_t *bs, uint32_t *buf, uint16_t *table) {
    (void)table;
    const uint8_t *in = (const uint8_t *)bs;
    uint16_t count = (uint16_t)(in[0] | in[1] << 8), q = (uint16_t)(in[4] | in[5] << 8),
             ver = (uint16_t)(in[6] | in[7] << 8);
    buf[0] = 0x38000000u | count;
    uint16_t *out = (uint16_t *)(buf + 1);
    uint32_t nout = (uint32_t)count * 2, k = 0;
    Bits b = {in + 8, 0, 0};
    int32_t prev[3] = {0, 0, 0};
    for (int blk = 0; k < nout; blk = (blk + 1) % 6) {
        int comp = blk == 0 ? 0 : blk == 1 ? 1 : 2;   /* Cr, Cb, Y */
        int32_t dc;
        if (ver == 3) {
            if (peek(&b, 10) == 0x3FF) break;   /* fin del fotograma: el resto, 0xFE00 */
            int size = -1;
            for (int s = 0; s < 9 && size < 0; s++) {
                int len = comp == 2 ? DC_Y[s].len : DC_C[s].len, code = comp == 2 ? DC_Y[s].code : DC_C[s].code;
                if ((int)peek(&b, len) == code) { skip(&b, len); size = s; }
            }
            if (size < 0) return -1;
            int32_t diff = 0;
            if (size) {
                diff = (int32_t)get(&b, size);
                if (diff < (1 << (size - 1))) diff -= (1 << size) - 1;
            }
            prev[comp] += diff * 4;
            dc = prev[comp];
        } else {
            dc = (int32_t)get(&b, 10);
        }
        out[k++] = (uint16_t)((q << 10) | (dc & 0x3FF));
        while (k < nout) {
            if (peek(&b, 2) == 2) { skip(&b, 2); out[k++] = 0xFE00; break; }   /* fin de bloque */
            if (peek(&b, 6) == 1) {   /* escape: run de 6 bits y nivel de 10 */
                skip(&b, 6);
                uint32_t run = get(&b, 6), lev = get(&b, 10);
                out[k++] = (uint16_t)((run << 10) | lev);
                continue;
            }
            int found = 0;
            for (unsigned i = 0; i < sizeof AC / sizeof AC[0]; i++)
                if (peek(&b, AC[i].len) == AC[i].code) {
                    skip(&b, AC[i].len);
                    int lev = get(&b, 1) ? -AC[i].level : AC[i].level;
                    out[k++] = (uint16_t)((AC[i].run << 10) | (lev & 0x3FF));
                    found = 1;
                    break;
                }
            if (!found) return -1;
        }
    }
    while (k < nout) out[k++] = 0xFE00;
    return 0;
}


/* ---- MDEC (decodificador de video por hardware), segun psx-spx ----
 * Comando de decodificacion: por macrobloque Cr, Cb, Y1..Y4 (16x16); cada bloque se
 * descuantiza (escala de los bits 10-15 del DC), se coloca en zigzag, pasa por la IDCT de
 * dos pasadas con la tabla de escala y se convierte de YUV a RGB. Salida: macrobloques de
 * 16x16 en orden de filas, 15 bits (256 medias palabras) o 24 bits (768 bytes).
 * Tablas: las que carga DecDCTReset, generadas por formula y comprobadas contra las de la
 * libreria del ACE.BIN (0x800B9938 y 0x800B99BC): cuantizacion = matriz intra de MPEG-1 en
 * zigzag con q[0] = 2; escala = floor(32768 * C(u) * cos((2x+1)u*pi/16)).
 * La conversion YUV->RGB usa coma fija de 10 bits (1,402; 0,3437; 0,7143; 1,772): SIN
 * VERIFICAR la aritmetica exacta del hardware (se compara con libav por PSNR).
 * Tiempo: el MDEC es instantaneo; el callback de fin de DecDCTout se entrega al terminar
 * la llamada (los callbacks que piden otra transferencia se encadenan sin anidar). */
#include <math.h>
#include "psx_libetc.h"

static const uint8_t ZZ[64] = {0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48,
                               41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                               30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
static const uint8_t MPEG_INTRA[64] = {8, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27,
                                       29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48,
                                       26, 27, 29, 32, 35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38,
                                       46, 56, 69, 83};
#define MDEC_OUT_MAX (640 * 512 * 3)

static struct {
    uint8_t qy[64], qc[64];
    int32_t scale[64];
    uint8_t out[MDEC_OUT_MAX];
    uint32_t out_len, out_pos;
    uint32_t out_cb;            /* DecDCToutCallback (direccion PS1) */
    int in_cb, pending;         /* encadenado de callbacks */
} M;

static void mdec_tables(void) {
    for (int k = 0; k < 64; k++) M.qy[k] = M.qc[k] = MPEG_INTRA[ZZ[k]];
    M.qy[0] = M.qc[0] = 2;
    for (int u = 0; u < 8; u++)
        for (int x = 0; x < 8; x++)
            M.scale[u * 8 + x] = (int32_t)floor(32768.0 * (u ? 1.0 : 1.0 / sqrt(2.0)) * cos((2 * x + 1) * u * M_PI / 16));
}

static int32_t s10(uint16_t n) { return (int32_t)((uint32_t)(n & 0x3FF) << 22) >> 22; }

/* un bloque: codigos run-level -> 64 valores tras la IDCT; devuelve palabras consumidas */
static const uint16_t *rl_block(const uint16_t *src, const uint16_t *end, const uint8_t *qt, int32_t *blk) {
    int32_t c[64] = {0}, tmp[64];
    while (src < end && *src == 0xFE00) src++;
    if (src >= end) { memset(blk, 0, 64 * sizeof *blk); return src; }
    uint16_t n = *src++;
    int k = 0, q = (n >> 10) & 63;
    int32_t val = s10(n) * qt[0];
    for (;;) {
        if (q == 0) val = s10(n) * 2;
        if (val < -0x400) val = -0x400;
        if (val > 0x3FF) val = 0x3FF;
        if (q) c[ZZ[k]] = val; else c[k] = val;
        if (src >= end) break;
        n = *src++;
        k += ((n >> 10) & 63) + 1;
        if (k > 63) break;
        val = (s10(n) * qt[k] * q + 4) / 8;
    }
    /* IDCT de psx-spx: dos pasadas, cada una traspone */
    int32_t *in = c, *out = tmp;
    for (int pass = 0; pass < 2; pass++) {
        for (int x = 0; x < 8; x++)
            for (int y = 0; y < 8; y++) {
                int64_t sum = 0;
                for (int z = 0; z < 8; z++) sum += (int64_t)in[y + z * 8] * (M.scale[x + z * 8] >> 3);
                out[x + y * 8] = (int32_t)((sum + 0xFFF) >> 13);
            }
        int32_t *t = in; in = out; out = t;
    }
    memcpy(blk, in, 64 * sizeof *blk);
    return src;
}

static int clamp8(int v) { return v < -128 ? -128 : v > 127 ? 127 : v; }

/* decodifica todo el flujo de un DecDCTin en M.out */
static void mdec_decode(const uint32_t *buf) {
    uint32_t hdr = buf[0], words = hdr & 0xFFFF;
    int depth = (hdr >> 27) & 3, sign = (hdr >> 26) & 1, b15 = (hdr >> 25) & 1;
    const uint16_t *src = (const uint16_t *)(buf + 1), *end = src + words * 2;
    M.out_len = M.out_pos = 0;
    while (src < end) {
        int32_t cr[64], cb[64], y[4][64];
        const uint16_t *s0 = src;
        src = rl_block(src, end, M.qc, cr);
        src = rl_block(src, end, M.qc, cb);
        for (int i = 0; i < 4; i++) src = rl_block(src, end, M.qy, y[i]);
        if (src == s0 || src > end) break;
        for (int py = 0; py < 16; py++)
            for (int px = 0; px < 16; px++) {
                int ci = (px >> 1) + (py >> 1) * 8;
                int32_t R = cr[ci], B = cb[ci];
                int32_t G = (-352 * B - 731 * R) >> 10;
                R = (1436 * R) >> 10;
                B = (1815 * B) >> 10;
                int32_t Y = y[(py >> 3) * 2 + (px >> 3)][(px & 7) + (py & 7) * 8];
                int r = clamp8(Y + R), g = clamp8(Y + G), bb = clamp8(Y + B);
                if (!sign) { r += 128; g += 128; bb += 128; }
                if (M.out_len + 3 > MDEC_OUT_MAX) return;
                if (depth == 3) {   /* 15 bits */
                    uint16_t v = (uint16_t)(((r & 0xFF) >> 3) | (((g & 0xFF) >> 3) << 5) | (((bb & 0xFF) >> 3) << 10) |
                                            (b15 << 15));
                    M.out[M.out_len++] = (uint8_t)v;
                    M.out[M.out_len++] = (uint8_t)(v >> 8);
                } else {           /* 24 bits */
                    M.out[M.out_len++] = (uint8_t)r;
                    M.out[M.out_len++] = (uint8_t)g;
                    M.out[M.out_len++] = (uint8_t)bb;
                }
            }
    }
}

void DecDCTReset(int mode) { (void)mode; mdec_tables(); M.out_len = M.out_pos = 0; M.pending = 0; }

/* mode bit 0: 0 = 15 bits, 1 = 24 bits; bit 1: poner el bit 15 (como el original 0x800AAE30) */
void DecDCTin(uint32_t *buf, int mode) {
    if (!M.scale[1]) mdec_tables();
    if (mode & 1) buf[0] &= ~0x08000000u; else buf[0] |= 0x08000000u;
    if (mode & 2) buf[0] |= 0x02000000u; else buf[0] &= ~0x02000000u;
    mdec_decode(buf);
}

void DecDCTout(uint32_t *buf, int size) {
    uint32_t n = (uint32_t)size * 4;
    if (M.out_pos + n > M.out_len) n = M.out_len > M.out_pos ? M.out_len - M.out_pos : 0;
    memcpy(buf, M.out + M.out_pos, n);
    M.out_pos += n;
    if (!M.out_cb) return;
    if (M.in_cb) { M.pending++; return; }   /* pedido desde el callback: se encadena */
    M.in_cb = 1;
    M.pending = 1;
    while (M.pending) { M.pending--; psx_call(M.out_cb, 0, 0); }
    M.in_cb = 0;
}

int DecDCTinSync(int mode) { (void)mode; return 0; }
int DecDCToutSync(int mode) { (void)mode; return 0; }
uint32_t DecDCToutCallback(uint32_t func) { uint32_t o = M.out_cb; M.out_cb = func; return o; }
