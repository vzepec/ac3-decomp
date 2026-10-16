/* Decodificacion de los bancos de sonido del SPU (port de tools/acesnd.py). */
#include "sound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

static const int FILT[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

/* ADPCM del SPU desde `off` hasta el bloque con el flag de fin (bit 0 del byte 1).
 * Bloques de 16 B: filtro/desplazamiento, flags (bit 2 inicio de bucle, bit 1 repetir). */
static int decode(Sound *s, const uint8_t *data, uint32_t size, uint32_t off) {
    uint32_t blocks = 0;
    for (uint32_t o = off; o + 16 <= size; o += 16) {
        blocks++;
        if (data[o + 1] & 1) break;
    }
    if (!blocks) return -1;
    s->pcm = malloc(blocks * 28 * sizeof(int16_t));
    if (!s->pcm) return -1;
    int s1 = 0, s2 = 0;
    uint32_t n = 0;
    s->loops = 0;
    s->loop_start = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        const uint8_t *q = data + off + 16 * b;
        int f = q[0] >> 4, sh = q[0] & 15;
        if (f > 4) f = 0;
        if (sh > 12) sh = 12;
        if (q[1] & 4) s->loop_start = n;
        if ((q[1] & 3) == 3) s->loops = 1;   /* fin + repetir: vuelve al inicio del bucle */
        for (int k = 2; k < 16; k++) {
            for (int h = 0; h < 2; h++) {
                int nib = h ? q[k] >> 4 : q[k] & 15;
                int v = (nib >= 8 ? nib - 16 : nib) * 4096;
                v = (v >> sh) + ((s1 * FILT[f][0] + s2 * FILT[f][1] + 32) >> 6);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                s->pcm[n++] = (int16_t)v;
                s2 = s1;
                s1 = v;
            }
        }
    }
    s->n = n;
    return 0;
}

int sound_load_bank(Sound *slots, const uint8_t *bank, uint32_t len) {
    if (len < 12) return -1;
    uint32_t b0 = bank[0], n = bank[1], base = rd32(bank + 4), size = rd32(bank + 8);
    if (!n || size % 16 || 12 + 20 * n + size > len || b0 + n > SOUND_SLOTS) return -1;
    const uint8_t *adpcm = bank + 12 + 20 * n;
    int loaded = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = bank + 12 + 20 * i;
        uint32_t addr = rd32(e + 16);
        if (addr < base || addr - base >= size) continue;
        Sound *s = &slots[b0 + i];
        free(s->pcm);
        memset(s, 0, sizeof *s);
        if (decode(s, adpcm, size, addr - base)) continue;
        /* nota de reproduccion (campo "vol", 0x3C00 = 60), nota base e[11] y afinacion u0
         * (e[10]): _spu_note2pitch SUMA las partes bajas en 1/128 de semitono (ver
         * tools/acesnd.py; corregido en la etapa 5) */
        uint16_t vol = rd16(e + 8);
        float semis = (float)(vol >> 8) - e[11] + ((vol & 0xFF) + e[10]) / 128.0f;
        s->rate = 44100.0f * powf(2.0f, semis / 12.0f);
        int vl = rd16(e + 4), vr = rd16(e + 6);
        s->vol = ((vl > vr ? vl : vr) & 0x3FFF) / 16383.0f;
        loaded++;
    }
    return loaded;
}

void sound_free_slots(Sound *slots, int first, int last) {
    for (int i = first; i <= last && i < SOUND_SLOTS; i++) {
        free(slots[i].pcm);
        memset(&slots[i], 0, sizeof slots[i]);
    }
}
