/* Demo de la capa PsyQ (ver psxdemo.h). */
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "music.h"
#include "psx_display.h"
#include "psx_gpu.h"
#include "psx_gte.h"
#include "psx_libgpu.h"
#include "psx_libgte.h"
#include "psx_libpress.h"
#include "psx_mem.h"
#include "psx_xa.h"
#include "psxdemo.h"

#define MAXPIX (320 * 240)
#define QLEN 8

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void sleep_ms(int ms) { struct timespec ts = {0, ms * 1000000L}; nanosleep(&ts, NULL); }

static struct {
    int which, active;
    /* pelicula */
    pthread_t thread;
    int have_thread;
    atomic_int run, done;
    int fd;
    uint32_t start, count;
    uint8_t chan;
    uint32_t frames[QLEN][MAXPIX];
    int fw[QLEN], fh[QLEN];
    uint32_t ts[QLEN];
    atomic_uint qr, qw;
    int shown;          /* ranura mostrada (-1 ninguna) */
    /* terreno */
    uint8_t *tm, *tl;
    uint32_t tl_len, ntiles, st[512], en[512];
    float cx, cy, cz, yaw;
    uint32_t img[MAXPIX];
} D = {.shown = -1};

/* ---------------------------------------------------------------- pelicula */

/* un fotograma completo: como el reproductor del juego (0x800555C0 / 0x8005587C) */
static void movie_decode(const uint8_t *data, uint32_t len, int w, int h, int slot) {
    uint8_t *bs = psx_ram + 0x100000, *rl = psx_ram + 0x110000, *sl = psx_ram + 0x180000;
    memcpy(bs, data, len);
    memset(bs + len, 0, 64);
    DecDCTvlc2((uint32_t *)bs, (uint32_t *)rl, NULL);
    DecDCTin((uint32_t *)rl, 1);   /* 24 bits */
    int mbw = (w + 15) / 16, mbh = (h + 15) / 16;
    for (int mx = 0; mx < mbw; mx++) {
        DecDCTout((uint32_t *)sl, mbh * 16 * 16 * 3 / 4);
        RECT r = {(int16_t)(mx * 24), 0, 24, (int16_t)(mbh * 16)};
        LoadImage(&r, (uint32_t *)sl);
    }
    DISPENV d;
    memset(&d, 0, sizeof d);
    d.disp.w = (int16_t)w; d.disp.h = (int16_t)h; d.isrgb24 = 1;
    PutDispEnv(&d);
    SetDispMask(1);
    psx_display_frame(D.frames[slot], MAXPIX, &D.fw[slot], &D.fh[slot]);
}

static void *movie_worker(void *arg) {
    (void)arg;
    PsxXa xa;
    psx_xa_reset(&xa);
    static uint8_t chunks[16][2016], frame[16 * 2016];
    uint32_t have = 0, cur_fn = 0xFFFFFFFFu;
    int16_t pcm[2 * 6000];
    uint8_t raw[2352];
    DecDCTReset(0);
    for (uint32_t k = 0; k < D.count && atomic_load(&D.run); k++) {
        if (pread(D.fd, raw, 2352, (off_t)(D.start + k) * 2352) != 2352) break;
        if (raw[15] != 2) continue;
        uint8_t sub = raw[18];
        if (sub & 4) {   /* audio XA del canal de la pelicula */
            if (raw[17] != D.chan) continue;
            int n = psx_xa_decode44(&xa, raw, pcm, 6000);
            while (n && !music_push(pcm, n) && atomic_load(&D.run)) sleep_ms(5);
            continue;
        }
        if (!(sub & 8)) continue;
        /* video: cabecera de 32 bytes de Namco (tools/psyqcheck_press.py) */
        int ci = raw[25], nch = rd16(raw + 26), fn = rd16(raw + 30), w = rd16(raw + 32), h = rd16(raw + 34);
        if (!nch || nch > 16 || ci >= nch || w > 320 || h > 240) continue;
        if ((uint32_t)fn != cur_fn) { cur_fn = (uint32_t)fn; have = 0; }
        memcpy(chunks[ci], raw + 56, 2016);
        have |= 1u << ci;
        if (have != (1u << nch) - 1) continue;
        for (int i = 0; i < nch; i++) memcpy(frame + i * 2016, chunks[i], 2016);
        have = 0;
        if (frame[2] != 0x00 || frame[3] != 0x38) continue;
        while (atomic_load(&D.qw) - atomic_load(&D.qr) >= QLEN - 1 && atomic_load(&D.run)) sleep_ms(4);
        int slot = (int)(atomic_load(&D.qw) % QLEN);
        movie_decode(frame, (uint32_t)nch * 2016, w, h, slot);
        D.ts[slot] = music_pushed();   /* posicion en el audio del flujo en ese punto */
        atomic_fetch_add(&D.qw, 1);
    }
    atomic_store(&D.done, 1);
    return NULL;
}

/* ---------------------------------------------------------------- terreno */

static uint8_t *section(const Record *r, int k, uint32_t *len) {
    uint32_t n;
    const uint8_t *raw = record_section(r, k, &n);
    if (!raw || !n) return NULL;
    if (is_ulz(raw)) return ulz_decompress(raw, n, len);
    uint8_t *c = malloc(n);
    memcpy(c, raw, n);
    *len = n;
    return c;
}

static void upload_tims(const uint8_t *b, uint32_t len) {
    for (uint32_t off = 0; off + 8 <= len; off += 4) {
        uint32_t flags = rd32(b + off + 4);
        if (rd32(b + off) != 0x10 || (flags & ~0xFu) || (flags & 7) > 3) continue;
        uint32_t p = off + 8;
        int ok = 1;
        for (int blk = (flags & 8) ? 0 : 1; blk < 2 && ok; blk++) {
            if (p + 12 > len) { ok = 0; break; }
            uint32_t bn = rd32(b + p);
            RECT r = {(int16_t)rd16(b + p + 4), (int16_t)rd16(b + p + 6), (int16_t)rd16(b + p + 8), (int16_t)rd16(b + p + 10)};
            if (!r.w || !r.h || bn != 12u + 2u * r.w * r.h || p + bn > len || r.x + r.w > 1024 || r.y + r.h > 512) { ok = 0; break; }
            memcpy(psx_ram + 0x180000, b + p + 12, bn - 12);
            LoadImage(&r, (uint32_t *)(psx_ram + 0x180000));
            p += bn;
        }
        if (ok) off = p - 4;
    }
}

static int terrain_load(Disc *d, AcePack *pk, uint32_t record) {
    uint32_t size, len;
    uint8_t *raw = pack_read_record(d, pk, record, &size);
    Record r;
    if (!raw || record_parse(&r, raw, size)) { free(raw); return -1; }
    ResetGraph(0);
    for (int k = 10; k <= 14; k++) { uint8_t *t = section(&r, k, &len); if (t) { upload_tims(t, len); free(t); } }
    free(D.tm); free(D.tl);
    D.tm = section(&r, 0, &len);
    D.tl = section(&r, 1, &D.tl_len);
    free(raw);
    if (!D.tm || !D.tl) return -1;
    D.ntiles = offset_table(D.tl, D.tl_len, D.st, D.en, 511);
    return 0;
}

#define OT_LEN 1024
#define VIEW 7

/* el bucle de terreno de psx/test/test_scene.c: RTPT, NCLIP, POLY_FT4/GT4, OT, DrawOTag */
static void terrain_frame(void) {
    int yaw = (int)(D.yaw * 4096 / (2 * M_PI)) & 0xFFF, pitch = -0x140;   /* negativo: mirar hacia abajo */
    SVECTOR ang = {(int16_t)pitch, (int16_t)yaw, 0, 0};
    MATRIX cam, view;
    RotMatrixYXZ(&ang, &cam);
    TransposeMatrix(&cam, &view);
    PsxGte *g = psx_gte_get_context();
    InitGeom();
    psx_gte_write_ctrl(g, 24, 160u << 16);
    psx_gte_write_ctrl(g, 25, 120u << 16);
    psx_gte_write_ctrl(g, 26, 220);
    DRAWENV de;
    SetDefDrawEnv(&de, 0, 0, 320, 240);
    de.isbg = 1; de.r0 = 90; de.g0 = 110; de.b0 = 140;
    PutDrawEnv(&de);
    uint32_t *ot = (uint32_t *)(psx_ram + 0x100000);
    ClearOTagR(ot, OT_LEN);
    uint8_t *next = psx_ram + 0x110000;
    double cx = D.cx, cy = D.cy, cz = D.cz;
    int ccx = (int)floor(cx / 2048) + 128, ccz = (int)floor(cz / 2048) + 128;
    static int32_t sxy[256], sz[256];
    for (int tz = ccz - VIEW; tz <= ccz + VIEW; tz++)
        for (int tx = ccx - VIEW; tx <= ccx + VIEW; tx++) {
            if (tx < 0 || tx > 255 || tz < 0 || tz > 255) continue;
            uint32_t t = rd16(D.tm + 2 * (tz * 256 + tx)) & 0x1FF;
            if (!t || t > D.ntiles) continue;
            const uint8_t *m = D.tl + D.st[t - 1];
            uint32_t ml = D.en[t - 1] - D.st[t - 1], ov = rd32(m + 4), op = rd32(m + 12), oe = rd32(m + 16);
            VECTOR rel = {(int32_t)((tx - 128) * 2048 + 1024 - cx), (int32_t)(0 - cy), (int32_t)((tz - 128) * 2048 + 1024 - cz), 0};
            VECTOR tr;
            ApplyMatrixLV(&view, &rel, &tr);
            if (tr.vz < -2048) continue;
            MATRIX vm = view;
            vm.t[0] = tr.vx; vm.t[1] = tr.vy; vm.t[2] = tr.vz;
            SetRotMatrix(&vm);
            SetTransMatrix(&vm);
            uint32_t nv = rd32(m + ov);
            if (nv > 255) continue;
            for (uint32_t gi = 0; gi * 3 < nv; gi++) {
                const uint8_t *q = m + ov + 4 + 20 * gi;
                for (int k = 0; k < 3; k++) {
                    psx_gte_write_data(g, 2 * k, rd32(q + 4 * k));
                    psx_gte_write_data(g, 2 * k + 1, (uint32_t)(int32_t)(int16_t)rd16(q + 12 + 2 * k));
                }
                psx_gte_command(g, 0x0280030);
                for (int k = 0; k < 3; k++) {
                    sxy[3 * gi + k] = (int32_t)psx_gte_read_data(g, 12 + k);
                    sz[3 * gi + k] = (int32_t)psx_gte_read_data(g, 17 + k);
                }
            }
            for (int blk = 0; blk < 2; blk++) {
                uint32_t off = blk ? oe : op, psz = blk ? 48 : 36, np = rd32(m + off);
                for (uint32_t j = 0; j < np && off + 4 + psz * (j + 1) <= ml; j++) {
                    const uint8_t *q = m + off + 4 + psz * j;
                    int i0 = q[16], i1 = q[17], i2 = q[18], i3 = q[19];
                    if (i0 >= (int)nv || i1 >= (int)nv || i2 >= (int)nv || i3 >= (int)nv) continue;
                    if (!sz[i0] || !sz[i1] || !sz[i2] || !sz[i3]) continue;
                    if (NormalClip(sxy[i0], sxy[i1], sxy[i2]) <= 0 && NormalClip(sxy[i1], sxy[i3], sxy[i2]) <= 0) continue;
                    int zmax = sz[i0];
                    if (sz[i1] > zmax) zmax = sz[i1];
                    if (sz[i2] > zmax) zmax = sz[i2];
                    if (sz[i3] > zmax) zmax = sz[i3];
                    int oz = zmax >> 5;
                    if (oz >= OT_LEN || next > psx_ram + 0x170000) continue;
                    uint32_t *p = (uint32_t *)next;
                    if (!blk) {
                        p[1] = (rd32(q + 32) & 0xFFFFFF) | 0x2C000000u;
                        p[2] = (uint32_t)sxy[i0]; p[3] = rd32(q + 20);
                        p[4] = (uint32_t)sxy[i1]; p[5] = rd32(q + 24);
                        p[6] = (uint32_t)sxy[i2]; p[7] = rd16(q + 28);
                        p[8] = (uint32_t)sxy[i3]; p[9] = rd16(q + 30);
                        p[0] = (9u << 24) | (ot[oz] & 0xFFFFFF);
                        next += 40;
                    } else {
                        p[1] = (rd32(q + 32) & 0xFFFFFF) | 0x3C000000u;
                        p[2] = (uint32_t)sxy[i0]; p[3] = rd32(q + 20);
                        p[4] = rd32(q + 36) & 0xFFFFFF; p[5] = (uint32_t)sxy[i1]; p[6] = rd32(q + 24);
                        p[7] = rd32(q + 40) & 0xFFFFFF; p[8] = (uint32_t)sxy[i2]; p[9] = rd16(q + 28);
                        p[10] = rd32(q + 44) & 0xFFFFFF; p[11] = (uint32_t)sxy[i3]; p[12] = rd16(q + 30);
                        p[0] = (12u << 24) | (ot[oz] & 0xFFFFFF);
                        next += 52;
                    }
                    ot[oz] = psx_addr(p) & 0xFFFFFF;
                }
            }
        }
    DrawOTag(&ot[OT_LEN - 1]);
    DISPENV disp;
    SetDefDispEnv(&disp, 0, 0, 320, 240);
    PutDispEnv(&disp);
    SetDispMask(1);
}

/* ---------------------------------------------------------------- API */

int psxdemo_start(Disc *d, AcePack *p, int which, uint32_t record, float x, float y, float z) {
    psxdemo_stop();
    D.which = which;
    D.shown = -1;
    if (which == PSXDEMO_TERRAIN) {
        if (terrain_load(d, p, record)) return -1;
        D.cx = x; D.cy = y - 1500; D.cz = z; D.yaw = 0;
        D.active = 1;
        return 0;
    }
    if (sph_entry(d, which, &D.start, &D.count, &D.chan)) return -1;
    D.fd = fileno(d->f);
    music_stream_begin();
    atomic_store(&D.qr, 0);
    atomic_store(&D.qw, 0);
    atomic_store(&D.done, 0);
    atomic_store(&D.run, 1);
    if (pthread_create(&D.thread, NULL, movie_worker, NULL)) return -1;
    D.have_thread = 1;
    D.active = 1;
    return 0;
}

void psxdemo_stop(void) {
    atomic_store(&D.run, 0);
    if (D.have_thread) pthread_join(D.thread, NULL);
    D.have_thread = 0;
    if (D.active && D.which != PSXDEMO_TERRAIN) music_stream_begin();
    D.active = 0;
}

const uint32_t *psxdemo_frame(int *w, int *h, float dt) {
    if (!D.active) return NULL;
    if (D.which == PSXDEMO_TERRAIN) {
        D.yaw += dt * 0.35f;
        terrain_frame();
        psx_display_frame(D.img, MAXPIX, w, h);
        return D.img;
    }
    /* pelicula: la ranura qr es la que se muestra; se avanza mientras el siguiente fotograma
     * ya deberia estar en pantalla segun el audio que ha sonado. El hilo nunca escribe en la
     * ranura qr (deja siempre una libre), asi que no hay carrera con la imagen mostrada. */
    uint32_t played = music_played(), r = atomic_load(&D.qr), qw = atomic_load(&D.qw);
    if (r == qw) { if (D.shown < 0) return NULL; }
    else {
        while (r + 1 != qw && (int32_t)(D.ts[(r + 1) % QLEN] - played) <= 0) r++;
        atomic_store(&D.qr, r);
        if (D.shown >= 0 || (int32_t)(D.ts[r % QLEN] - played) <= 0) D.shown = (int)(r % QLEN);
    }
    if (D.shown < 0) return NULL;
    *w = D.fw[D.shown];
    *h = D.fh[D.shown];
    return D.frames[D.shown];
}
