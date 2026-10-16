/* XA-ADPCM (ver psx_xa.h). */
#include <string.h>

#include "psx_libcd.h"
#include "psx_libspu.h"
#include "psx_xa.h"

static const int K[4][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}};

void psx_xa_reset(PsxXa *x) { memset(x, 0, sizeof *x); }

int psx_xa_decode(PsxXa *x, const uint8_t *raw, int16_t *out, int *stereo, int *rate) {
    if (raw[15] != 2 || !(raw[18] & 0x04)) return 0;   /* modo 2 y submodo de audio */
    uint8_t ci = raw[19];
    int st = (ci & 3) == 1, bits8 = ((ci >> 4) & 3) == 1;
    *stereo = st;
    *rate = ((ci >> 2) & 3) == 1 ? 18900 : 37800;
    int units = bits8 ? 4 : 8, n = 0;
    for (int g = 0; g < 18; g++) {
        const uint8_t *grp = raw + 24 + 128 * g;
        for (int u = 0; u < units; u++) {
            uint8_t h = grp[4 + u];
            int shift = (bits8 ? 8 : 12) - (h & 15), f = (h >> 4) & 3;
            int ch = st ? (u & 1) : 0;
            int32_t s1 = x->s1[ch], s2 = x->s2[ch];
            for (int i = 0; i < 28; i++) {
                int32_t t;
                if (bits8) t = (int8_t)grp[16 + 4 * i + u];
                else t = (int32_t)((uint32_t)((grp[16 + 4 * i + u / 2] >> ((u & 1) * 4)) & 15) << 28) >> 28;
                int32_t smp = (shift >= 0 ? t * (1 << shift) : t >> -shift) + ((s1 * K[f][0] + s2 * K[f][1] + 32) >> 6);
                if (smp > 32767) smp = 32767;
                if (smp < -32768) smp = -32768;
                s2 = s1;
                s1 = smp;
                /* mono: las unidades van seguidas; estereo: pares L/R intercalados */
                if (st) out[2 * ((n / 2 / 28) * 28 + i) + ch] = (int16_t)smp;
                else out[n + i] = (int16_t)smp;
            }
            x->s1[ch] = s1;
            x->s2[ch] = s2;
            n += 28;
        }
    }
    return st ? n / 2 : n;
}

int psx_xa_decode44(PsxXa *x, const uint8_t *raw, int16_t *out, int max_frames) {
    int16_t pcm[4032];
    int st, rate, n = psx_xa_decode(x, raw, pcm, &st, &rate);
    if (!n) return 0;
    uint32_t step = (uint32_t)(((uint64_t)rate << 16) / 44100);
    int k = 0;
    /* posicion relativa a la muestra "last" (indice -1): pos 0 = last, 65536 = pcm[0] */
    while (k < max_frames) {
        uint32_t i = x->pos >> 16, fr = x->pos & 0xFFFF;
        if ((int)i >= n) break;
        for (int c = 0; c < 2; c++) {
            int cc = st ? c : 0;
            int32_t a = i == 0 ? x->last[c] : pcm[st ? 2 * (i - 1) + cc : i - 1];
            int32_t b = pcm[st ? 2 * i + cc : i];
            out[2 * k + c] = (int16_t)(a + (int32_t)(((int64_t)(b - a) * fr) >> 16));
        }
        k++;
        x->pos += step;
    }
    x->pos -= (uint32_t)n << 16;
    x->last[0] = pcm[st ? 2 * (n - 1) : n - 1];
    x->last[1] = pcm[st ? 2 * (n - 1) + 1 : n - 1];
    return k;
}

static PsxXa g_xa;

void psx_xa_to_spu(const uint8_t *raw) {
    int16_t out[2 * 6000];
    int n = psx_xa_decode44(&g_xa, raw, out, 6000);
    if (n) psx_spu_cd_push(psx_spu(), out, n);
}

void psx_audio_connect(void) {
    psx_xa_reset(&g_xa);
    psx_cd_set_xa_sink(psx_xa_to_spu);
}
