/* Musica XA (ver music.h). */
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "music.h"
#include "psx_xa.h"

/* entrada del ACE.SPH por registro de mision 6..46 (tools/acemusic.py --c): la que usa el
 * juego en las demostraciones (0x800B0540 -> 0x800B049C de ACE.BIN). En partida el guion
 * elige la pista con FUN_800550F0; que indice pide cada mision esta SIN RESOLVER. */
static const uint8_t MISSION_TRACK[41] = {16, 23, 21, 20, 35, 26, 13, 5, 32, 16, 24, 8, 19, 12, 21, 17, 13, 36, 30, 16, 26,
                                          12, 22, 18, 16, 24, 23, 8, 27, 35, 33, 32, 14, 9, 7, 20, 13, 32, 3, 9, 24};

#define RING (44100 * 2)   /* 2 s de muestras estereo */

static int16_t g_ring[RING][2];
static atomic_uint g_r, g_w;
static atomic_int g_run;
static pthread_t g_thread;
static int g_have_thread;
static struct { int fd; uint32_t start, count; uint8_t chan; } g_trk;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int music_track_for_mission(uint32_t record) {
    return record >= 6 && record <= 46 ? MISSION_TRACK[record - 6] : -1;
}

static void sleep_ms(int ms) {
    struct timespec ts = {0, ms * 1000000L};
    nanosleep(&ts, NULL);
}

static void *worker(void *arg) {
    (void)arg;
    PsxXa xa;
    psx_xa_reset(&xa);
    uint8_t raw[2352];
    int16_t pcm[2 * 6000];
    uint32_t k = 0;
    while (atomic_load(&g_run)) {
        if (RING - (atomic_load(&g_w) - atomic_load(&g_r)) < 6000) { sleep_ms(20); continue; }
        if (k >= g_trk.count) { k = 0; psx_xa_reset(&xa); }   /* la pista se repite */
        if (pread(g_trk.fd, raw, 2352, (off_t)(g_trk.start + k) * 2352) != 2352) break;
        k++;
        if (raw[15] != 2 || raw[17] != g_trk.chan || !(raw[18] & 4)) continue;
        int n = psx_xa_decode44(&xa, raw, pcm, 6000);
        uint32_t w = atomic_load(&g_w);
        for (int i = 0; i < n; i++) { g_ring[(w + i) % RING][0] = pcm[2 * i]; g_ring[(w + i) % RING][1] = pcm[2 * i + 1]; }
        atomic_store(&g_w, w + (uint32_t)n);
    }
    return NULL;
}

int sph_entry(Disc *d, int entry, uint32_t *start, uint32_t *count, uint8_t *chan) {
    if (entry < 0 || !d->f || d->sector_size != 2352) return -1;
    DiscFile sph, spb;
    if (disc_find(d, "ACE.SPH", &sph) || disc_find(d, "ACE.SPB", &spb)) return -1;
    uint8_t *idx = malloc(sph.size + 2048);
    if (!idx || disc_read(d, sph.lba, idx, sph.size)) { free(idx); return -1; }
    uint32_t n = rd32(idx + 0x10);
    if (memcmp(idx, "AC3E", 4) || (uint32_t)entry >= n) { free(idx); return -1; }
    uint32_t tag = rd32(idx + 0x14 + 8 * entry), sec = rd32(idx + 0x18 + 8 * entry);
    free(idx);
    /* tag del SPH (tools/acepack.py spb_tag): canal en los bits 19-23, extension en sectores
     * = longitud (bits 0-18) << desplazamiento (bits 24-26) */
    *start = spb.lba + sec;
    *count = (tag & 0x7FFFF) << ((tag >> 24) & 7);
    *chan = (uint8_t)((tag >> 19) & 31);
    return 0;
}

int music_start(Disc *d, int entry) {
    music_stop();
    if (sph_entry(d, entry, &g_trk.start, &g_trk.count, &g_trk.chan)) return -1;
    g_trk.fd = fileno(d->f);
    atomic_store(&g_r, 0);
    atomic_store(&g_w, 0);
    atomic_store(&g_run, 1);
    if (pthread_create(&g_thread, NULL, worker, NULL)) { atomic_store(&g_run, 0); return -1; }
    g_have_thread = 1;
    return 0;
}

void music_stream_begin(void) {
    music_stop();
    atomic_store(&g_r, 0);
    atomic_store(&g_w, 0);
}

int music_push(const int16_t *st, int frames) {
    uint32_t r = atomic_load(&g_r), w = atomic_load(&g_w);
    if (RING - (w - r) < (uint32_t)frames) return 0;
    for (int i = 0; i < frames; i++) { g_ring[(w + i) % RING][0] = st[2 * i]; g_ring[(w + i) % RING][1] = st[2 * i + 1]; }
    atomic_store(&g_w, w + (uint32_t)frames);
    return frames;
}

uint32_t music_pushed(void) { return atomic_load(&g_w); }
uint32_t music_played(void) { return atomic_load(&g_r); }

void music_stop(void) {
    atomic_store(&g_run, 0);
    if (g_have_thread) pthread_join(g_thread, NULL);
    g_have_thread = 0;
    atomic_store(&g_r, atomic_load(&g_w));
}

void music_mix(float *acc, int frames, float vol) {
    uint32_t r = atomic_load(&g_r), w = atomic_load(&g_w);
    int n = (int)(w - r) < frames ? (int)(w - r) : frames;
    for (int i = 0; i < n; i++) {
        acc[2 * i] += g_ring[(r + i) % RING][0] * vol;
        acc[2 * i + 1] += g_ring[(r + i) % RING][1] * vol;
    }
    atomic_store(&g_r, r + (uint32_t)n);
}
