/* Interpolacion de imagenes entre pasos de logica (ver psx_interp.h). */
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "psx_display.h"
#include "psx_gpu.h"
#include "psx_gte.h"
#include "psx_interp.h"
#include "psx_libetc.h"
#include "psx_mem.h"

#define MAXV 65536            /* vertices de la GTE por paso */
#define HBITS 17
#define HSIZE (1 << HBITS)
#define MAXREF 131072         /* coordenadas de la lista por paso */
#define JUMP 48               /* pixeles: un vertice que salta mas no se interpola */

int psx_interp_on;
static int g_nfrac, g_min_vb;
static PsxInterpSink g_sink;

/* vertices de este paso y del anterior: clave del modelo (x, y, z) y posicion de pantalla */
typedef struct { uint64_t base; uint32_t sxy; int next; } Vert;
static Vert cur[MAXV], prv[MAXV];
static int ncur, nprv;
static int16_t pdx[MAXV], pdy[MAXV];
static uint8_t pok[MAXV];

/* tablas hash con generacion (se "vacian" subiendo la generacion) */
typedef struct { uint64_t key; uint32_t val; uint32_t cnt; uint32_t gen; } Slot;
static Slot occ[HSIZE], prevt[HSIZE], valt[HSIZE];
static uint32_t occ_gen = 1, prev_gen = 1, val_gen = 1;
static int have_prev, last_vb = -1;
static int st_dup, st_gte, st_matched, st_paired, st_dragged, st_total;

static uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27; x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

static Slot *slot(Slot *t, uint32_t gen, uint64_t key, int insert) {
    uint32_t i = (uint32_t)key & (HSIZE - 1);
    for (int n = 0; n < HSIZE; n++, i = (i + 1) & (HSIZE - 1)) {
        if (t[i].gen != gen) {
            if (!insert) return NULL;
            t[i].gen = gen; t[i].key = key; t[i].val = 0; t[i].cnt = 0;
            return &t[i];
        }
        if (t[i].key == key) return &t[i];
    }
    return NULL;
}

void psx_interp_config(int nfrac, int min_vblanks, PsxInterpSink sink) {
    g_nfrac = nfrac < 0 ? 0 : nfrac > 7 ? 7 : nfrac;
    g_min_vb = min_vblanks;
    g_sink = sink;
    psx_interp_on = g_nfrac > 0 && sink;
    psx_gte_vertex_hook = psx_interp_on ? psx_interp_vertex : 0;
    have_prev = 0;
    ncur = 0;
}

void psx_interp_vertex(int32_t x, int32_t y, int32_t z, int32_t sx, int32_t sy) {
    if (ncur >= MAXV) return;
    uint64_t base = mix(((uint64_t)(uint16_t)x) | ((uint64_t)(uint16_t)y << 16) | ((uint64_t)(uint16_t)z << 32) | (1ull << 48));
    Slot *o = slot(occ, occ_gen, base, 1);
    if (o) { if (o->cnt) st_dup++; o->cnt++; }
    cur[ncur].base = base;
    cur[ncur].sxy = ((uint32_t)(uint16_t)sy << 16) | (uint16_t)sx;
    ncur++;
}

/* ---- recorrido de la lista: palabras de coordenadas de pantalla de poligonos, lineas y
 * rectangulos, agrupadas por primitiva ---- */
static uint32_t *ref[MAXREF];
static int ref_prim[MAXREF], nref, nprim;

static void add_ref(uint32_t *w) { if (nref < MAXREF) { ref[nref] = w; ref_prim[nref] = nprim; nref++; } }

static void walk_packet(uint32_t *p, int n) {
    int i = 0;
    while (i < n) {
        uint32_t c = p[i] >> 24;
        if (c >= 0x20 && c < 0x40) {                       /* poligono */
            int tex = c & 4, gour = c & 0x10, nv = (c & 8) ? 4 : 3;
            int len = 1 + nv * (1 + (tex ? 1 : 0)) + (gour ? nv - 1 : 0);
            if (i + len > n) return;
            int k = i + 1;
            for (int v = 0; v < nv; v++) {
                if (gour && v > 0) k++;
                add_ref(&p[k++]);
                if (tex) k++;
            }
            nprim++;
            i += len;
        } else if (c >= 0x40 && c < 0x60) {                /* linea */
            int gour = c & 0x10;
            if (c & 8) {                                   /* polilinea: hasta 0x5xxx5xxx */
                int k = i + 1, first = 1;
                while (k < n && ((p[k] & 0xF000F000u) != 0x50005000u || first)) {
                    if (gour && !first) k++;
                    if (k >= n) break;
                    add_ref(&p[k++]);
                    first = 0;
                }
                nprim++;
                i = k + 1;
            } else {
                int len = gour ? 4 : 3;
                if (i + len > n) return;
                add_ref(&p[i + 1]);
                add_ref(&p[i + (gour ? 3 : 2)]);
                nprim++;
                i += len;
            }
        } else if (c >= 0x60 && c < 0x80) {                /* rectangulo */
            int len = 2 + ((c & 4) ? 1 : 0) + (((c >> 3) & 3) == 0 ? 1 : 0);
            if (i + len > n) return;
            add_ref(&p[i + 1]);
            nprim++;
            i += len;
        } else if (c == 0x02) i += 3;
        else if (c >= 0x80 && c < 0xA0) i += 4;
        else if (c >= 0xA0 && c < 0xC0) {
            if (i + 3 > n) return;
            uint32_t wh = p[i + 2];
            int words = (int)(((wh & 0xFFFF) * (wh >> 16) + 1) / 2);
            i += 3 + words;
        } else if (c >= 0xC0 && c < 0xE0) i += 3;
        else i += 1;
    }
}

static void walk(uint32_t addr) {
    nref = nprim = 0;
    int guard = 0;
    while ((addr & 0xFFFFFF) != 0xFFFFFF && ++guard < 200000) {
        uint32_t *p = (uint32_t *)psx_ptr(addr);
        uint32_t tag = p[0];
        walk_packet(p + 1, (int)(tag >> 24));
        addr = 0x80000000u | (tag & 0xFFFFFF);
    }
}

static int16_t lo(uint32_t w) { return (int16_t)(w & 0xFFFF); }
static int16_t hi(uint32_t w) { return (int16_t)(w >> 16); }

static uint32_t orig[MAXREF];
static int16_t ddx[MAXREF], ddy[MAXREF];
static uint8_t moved[MAXREF];
static uint32_t img[1024 * 512];

static double g_tstep;   /* hora del paso en curso (CLOCK_MONOTONIC, s) */

static void capture(PsxGpu *g, int num, int den, int vb) {
    int w, h;
    psx_display_frame_at(g->area_x1, g->area_y1, img, 1024 * 512, &w, &h);
    if (w && h) g_sink(img, w, h, num, den, vb, g_tstep);
}

/* ---- hilos de dibujo (psx_interp_threads): el hilo del juego deja una instantanea del paso
 * (RAM, scratchpad, estado de la GPU antes del dibujo real y la lista de coordenadas que se
 * mueven) y sigue; cada hilo dibuja sus fases k sobre copias propias. Si al llegar el paso
 * siguiente algun hilo no ha terminado, ese paso va sin intermedias (se cuenta). ---- */
#define MAXW 4
static struct {
    pthread_mutex_t mu;
    pthread_cond_t go, done;
    uint32_t seq;                     /* numero de trabajo */
    int pending;                      /* hilos que no han terminado el trabajo actual */
    int nthreads;
    void (*on_start)(void);
    /* el trabajo */
    uint8_t ram[PSX_RAM_SIZE], scratch[0x400];
    PsxGpu gpu;
    uint32_t addr[MAXREF], orig[MAXREF];
    int16_t dx[MAXREF], dy[MAXREF];
    int n, den, interval, x0, y0, dw, dh, on, rgb24;
    uint32_t ot;
    double t;
    PsxInterpSink sink;
    /* rendimiento */
    double worker_s, worker_max;
    int jobs, skipped, steps, unpaired;
    double pair_s, snap_s, real_s;
} J = {.mu = PTHREAD_MUTEX_INITIALIZER, .go = PTHREAD_COND_INITIALIZER, .done = PTHREAD_COND_INITIALIZER};

static double tnow(void);

static void *worker(void *arg) {
    int id = (int)(intptr_t)arg;
    if (J.on_start) J.on_start();
    uint8_t *ram = malloc(PSX_RAM_SIZE), scratch[0x400];
    PsxGpu *gpu = malloc(sizeof *gpu);
    uint32_t *out = malloc(1024 * 512 * 4);
    uint32_t seen = 0;
    for (;;) {
        pthread_mutex_lock(&J.mu);
        while (J.seq == seen) pthread_cond_wait(&J.go, &J.mu);
        seen = J.seq;
        int nt = J.nthreads;
        pthread_mutex_unlock(&J.mu);
        if (id >= nt) continue;   /* hilo sobrante (se pidieron menos): no cuenta */
        double t0 = tnow();
        int copied = 0;
        for (int k = id + 1; k < J.den; k += nt) {
            if (!copied) { memcpy(ram, J.ram, PSX_RAM_SIZE); memcpy(scratch, J.scratch, sizeof scratch); copied = 1; }
            memcpy(gpu, &J.gpu, sizeof *gpu);
            int den = J.den;
            for (int i = 0; i < J.n; i++) {
                uint32_t a = J.addr[i];
                uint32_t *w = (uint32_t *)((a & 0xFFFFFC00u) == PSX_SCRATCH_ADDR ? scratch + (a & 0x3FC) : ram + (a & (PSX_RAM_SIZE - 4)));
                int ddx = J.dx[i], ddy = J.dy[i];
                int x = (int16_t)(J.orig[i] & 0xFFFF) + (ddx * (den - k) + (ddx >= 0 ? den / 2 : -den / 2)) / den;
                int y = (int16_t)(J.orig[i] >> 16) + (ddy * (den - k) + (ddy >= 0 ? den / 2 : -den / 2)) / den;
                *w = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
            }
            psx_gpu_draw_list_mem(gpu, J.ot, ram, scratch);
            psx_display_convert(gpu->vram, J.x0, J.y0, J.dw, J.dh, J.on, J.rgb24, out);
            J.sink(out, J.dw, J.dh, k, den, J.interval, J.t);
        }
        double dt = tnow() - t0;
        pthread_mutex_lock(&J.mu);
        J.worker_s += dt;
        if (dt > J.worker_max) J.worker_max = dt;
        if (--J.pending == 0) pthread_cond_broadcast(&J.done);
        pthread_mutex_unlock(&J.mu);
    }
    return NULL;
}

void psx_interp_threads(int n, void (*on_start)(void)) {
    static pthread_t th[MAXW];
    n = n < 0 ? 0 : n > MAXW ? MAXW : n;
    pthread_mutex_lock(&J.mu);
    while (J.pending) pthread_cond_wait(&J.done, &J.mu);
    J.on_start = on_start;
    int had = 0;
    for (int i = 0; i < MAXW; i++) had += th[i] != 0;
    for (int i = had; i < n; i++) pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
    /* los hilos no se destruyen: con menos, los sobrantes se quedan parados */
    J.nthreads = n;
    pthread_mutex_unlock(&J.mu);
}

void psx_interp_perf(PsxInterpPerf *p, int reset) {
    pthread_mutex_lock(&J.mu);
    p->steps = J.steps; p->jobs = J.jobs; p->skipped = J.skipped; p->nfrac = g_nfrac; p->threads = J.nthreads; p->unpaired = J.unpaired;
    int n = J.steps ? J.steps : 1;
    p->pair_ms = J.pair_s * 1000 / n; p->snap_ms = J.snap_s * 1000 / n; p->real_ms = J.real_s * 1000 / n;
    p->worker_ms = J.jobs ? J.worker_s * 1000 / (J.jobs * (J.nthreads ? J.nthreads : 1)) : 0;
    p->worker_max_ms = J.worker_max * 1000;
    if (reset) { J.steps = J.jobs = J.skipped = J.unpaired = 0; J.pair_s = J.snap_s = J.real_s = J.worker_s = J.worker_max = 0; }
    pthread_mutex_unlock(&J.mu);
}

static double tnow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double tm_pair, tm_draw, tm_cap, tm_copy, tm_real; static int tm_n;

int psx_interp_skip_test;   /* prueba: un paso de cada dos se dibuja sin usarse de referencia */

int psx_interp_draw(void *gpu, uint32_t ot) {
    PsxGpu *g = gpu;
    static int parity;
    if (psx_interp_skip_test && (parity++ & 1)) {   /* paso k+1: solo la imagen real (fase 0/0) */
        psx_gpu_draw_list(g, ot);
        capture(g, 0, 0, 1);
        ncur = 0;
        occ_gen++;
        last_vb = VSync(-1);
        return 1;
    }
    int vb = VSync(-1), interval = last_vb < 0 ? 0 : vb - last_vb;
    last_vb = vb;
    double ta = tnow();
    /* lo que salio de la GTE en este paso, por valor de pantalla */
    val_gen++;
    for (int i = 0; i < ncur; i++) {
        Slot *s = slot(valt, val_gen, mix(cur[i].sxy | (1ull << 40)), 1);
        if (s && s->val == 0) s->val = (uint32_t)i + 1;
    }
    walk(ot);
    if (getenv("PSX_INTERP_DBG")) {
        static int c;
        if (c++ % 50 == 0) fprintf(stderr, "interp: %d vertices, %d repetidos (misma x,y,z del modelo)\n", ncur, st_dup);
    }
    st_dup = 0;
    st_gte = ncur; st_matched = st_paired = st_dragged = 0; st_total = nref;
    /* 1. pares sin ambiguedad (clave unica en los dos pasos) -> movimiento global afin:
     *    d = P0 - P1 = A * (P1 - centro) + e, por minimos cuadrados con un rechazo */
    double A[2][3] = {{0, 0, 0}, {0, 0, 0}};
    int model = 0;
    if (have_prev) {
        for (int it = 0; it < 2; it++) {
            double M[3][3] = {{0}}, R[2][3] = {{0}};
            int n = 0;
            for (int i = 0; i < ncur; i++) {
                Slot *o = slot(occ, occ_gen, cur[i].base, 0), *p = slot(prevt, prev_gen, cur[i].base, 0);
                if (!o || o->cnt != 1 || !p || p->cnt != 1) continue;
                double x = lo(cur[i].sxy) - 160, y = hi(cur[i].sxy) - 128;
                double dx = lo(prv[p->val - 1].sxy) - lo(cur[i].sxy), dy = hi(prv[p->val - 1].sxy) - hi(cur[i].sxy);
                if (fabs(dx) > JUMP || fabs(dy) > JUMP) continue;
                if (it && (fabs(dx - (A[0][0] * x + A[0][1] * y + A[0][2])) > 6 || fabs(dy - (A[1][0] * x + A[1][1] * y + A[1][2])) > 6)) continue;
                double v[3] = {x, y, 1};
                for (int r = 0; r < 3; r++) {
                    for (int c = 0; c < 3; c++) M[r][c] += v[r] * v[c];
                    R[0][r] += v[r] * dx;
                    R[1][r] += v[r] * dy;
                }
                n++;
            }
            if (n < 24) break;
            /* resolver M a = R (3x3, Cramer) */
            double det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                         M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
            if (fabs(det) < 1e-6) break;
            for (int k = 0; k < 2; k++)
                for (int c = 0; c < 3; c++) {
                    double T[3][3];
                    memcpy(T, M, sizeof T);
                    for (int r = 0; r < 3; r++) T[r][c] = R[k][r];
                    A[k][c] = (T[0][0] * (T[1][1] * T[2][2] - T[1][2] * T[2][1]) - T[0][1] * (T[1][0] * T[2][2] - T[1][2] * T[2][0]) +
                               T[0][2] * (T[1][0] * T[2][1] - T[1][1] * T[2][0])) / det;
                }
            model = 1;
        }
    }
    /* 2. pareja de cada vertice: la unica, o entre las repetidas la mas cercana a la prediccion */
    for (int i = 0; i < ncur; i++) {
        pok[i] = 0;
        if (!have_prev) continue;
        Slot *p = slot(prevt, prev_gen, cur[i].base, 0);
        if (!p) continue;
        int x1 = lo(cur[i].sxy), y1 = hi(cur[i].sxy);
        double ex = 0, ey = 0;
        if (model) {
            ex = A[0][0] * (x1 - 160) + A[0][1] * (y1 - 128) + A[0][2];
            ey = A[1][0] * (x1 - 160) + A[1][1] * (y1 - 128) + A[1][2];
        }
        int best = -1;
        double bd = 1e9;
        for (int j = (int)p->val - 1; j >= 0; j = prv[j].next) {
            double dx = lo(prv[j].sxy) - x1 - ex, dy = hi(prv[j].sxy) - y1 - ey;
            double d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = j; }
        }
        if (best < 0) continue;
        int dx = lo(prv[best].sxy) - x1, dy = hi(prv[best].sxy) - y1;
        if (abs(dx) > JUMP || abs(dy) > JUMP) continue;
        if (p->cnt > 1 && model && bd > 12 * 12) continue;   /* repetido sin candidato creible */
        pdx[i] = (int16_t)dx; pdy[i] = (int16_t)dy; pok[i] = 1;
    }
    /* 3. desplazamiento de cada coordenada de la lista */
    for (int i = 0; i < nref; i++) {
        uint32_t w = *ref[i];
        orig[i] = w;
        ddx[i] = ddy[i] = 0;
        moved[i] = 0;
        Slot *s = slot(valt, val_gen, mix(w | (1ull << 40)), 0);
        if (!s || !s->val) continue;
        st_matched++;
        moved[i] = 1;
        int k = (int)s->val - 1;
        if (!pok[k]) continue;
        ddx[i] = pdx[k]; ddy[i] = pdy[k];
        moved[i] = 2;
        st_paired++;
    }
    /* sin pareja, en primitivas de la GTE: la media de las emparejadas de la primitiva, o si no
     * hay ninguna, el movimiento global */
    for (int a = 0; a < nref;) {
        int b = a, sx = 0, sy = 0, np = 0, any = 0;
        while (b < nref && ref_prim[b] == ref_prim[a]) {
            if (moved[b] == 2) { sx += ddx[b]; sy += ddy[b]; np++; }
            if (moved[b]) any = 1;
            b++;
        }
        if (any)
            for (int i = a; i < b; i++) {
                if (moved[i] == 2) continue;
                if (np) { ddx[i] = (int16_t)(sx / np); ddy[i] = (int16_t)(sy / np); }
                else if (model) {
                    int x1 = lo(orig[i]), y1 = hi(orig[i]);
                    ddx[i] = (int16_t)lrint(A[0][0] * (x1 - 160) + A[0][1] * (y1 - 128) + A[0][2]);
                    ddy[i] = (int16_t)lrint(A[1][0] * (x1 - 160) + A[1][1] * (y1 - 128) + A[1][2]);
                } else continue;
                moved[i] = 3; st_dragged++;
            }
        a = b;
    }
    /* el siguiente paso se compara con este: listas por clave */
    prev_gen++;
    nprv = ncur;
    for (int i = ncur - 1; i >= 0; i--) {
        prv[i] = cur[i];
        Slot *s = slot(prevt, prev_gen, cur[i].base, 1);
        if (!s) { prv[i].next = -1; continue; }
        prv[i].next = (int)s->val - 1;
        s->val = (uint32_t)i + 1;
        s->cnt++;
    }
    int ok = have_prev && interval >= g_min_vb && st_matched > 64 && st_paired * 4 >= st_matched;
    have_prev = 1;
    ncur = 0;
    occ_gen++;
    if (!ok) {
        /* paso sin intermedias (pocas parejas: giro brusco, el mapa que aparece...): la imagen
         * real tambien va al sumidero. Si no, la pantalla seguiria con la del paso anterior y,
         * pasados 0,15 s, saltaria a la otra via: imagen congelada y titileo (Retroid, 1.2).
         * Tambien el primer paso tras psx_interp_config (sin paso anterior) */
        if (psx_interp_skip_test || !g_sink || interval < g_min_vb) return 0;
        pthread_mutex_lock(&J.mu);
        J.unpaired++;
        pthread_mutex_unlock(&J.mu);
        g_tstep = ta;
        psx_gpu_draw_list(g, ot);
        capture(g, 1, 1, interval);
        return 1;
    }
    g_tstep = ta;
    if (J.nthreads > 0) {   /* hilos de dibujo */
        double tb = tnow();
        pthread_mutex_lock(&J.mu);
        int busy = J.pending > 0;
        J.steps++;
        J.pair_s += tb - ta;
        if (busy) J.skipped++;
        pthread_mutex_unlock(&J.mu);
        if (!busy) {
            memcpy(J.ram, psx_ram, PSX_RAM_SIZE);
            memcpy(J.scratch, psx_scratch, sizeof J.scratch);
            memcpy(&J.gpu, g, sizeof J.gpu);
            int n = 0;
            for (int i = 0; i < nref; i++) {
                if (!moved[i] || (!ddx[i] && !ddy[i])) continue;
                J.addr[n] = psx_addr(ref[i]); J.orig[n] = orig[i]; J.dx[n] = ddx[i]; J.dy[n] = ddy[i];
                n++;
            }
            J.n = n; J.ot = ot; J.den = g_nfrac + 1; J.interval = interval; J.t = ta; J.sink = g_sink;
            J.x0 = g->area_x1; J.y0 = g->area_y1;
            psx_display_geom(&J.dw, &J.dh, &J.on, &J.rgb24);
            if (J.dw * J.dh > 1024 * 512) J.dw = J.dh = 0;
            pthread_mutex_lock(&J.mu);
            if (J.dw > 0 && J.dh > 0) { J.pending = J.nthreads; J.seq++; J.jobs++; pthread_cond_broadcast(&J.go); }
            pthread_mutex_unlock(&J.mu);
        }
        double tc = tnow();
        psx_gpu_draw_list(g, ot);
        capture(g, g_nfrac + 1, g_nfrac + 1, interval);
        pthread_mutex_lock(&J.mu);
        J.snap_s += tc - tb;
        J.real_s += tnow() - tc;
        pthread_mutex_unlock(&J.mu);
        return 1;
    }
    /* imagenes intermedias: la lista con las posiciones mezcladas, sobre una copia del estado */
    double tb = tnow(); tm_pair += tb - ta;
    J.steps++;   /* sin hilos de dibujo: solo este hilo toca J */
    static PsxGpu saved;
    memcpy(&saved, g, sizeof saved);
    tm_copy += tnow() - tb;
    int den = g_nfrac + 1;
    for (int k = 1; k <= g_nfrac; k++) {
        for (int i = 0; i < nref; i++) {
            if (!moved[i] || (!ddx[i] && !ddy[i])) continue;
            int x = lo(orig[i]) + (ddx[i] * (den - k) + (ddx[i] >= 0 ? den / 2 : -den / 2)) / den;
            int y = hi(orig[i]) + (ddy[i] * (den - k) + (ddy[i] >= 0 ? den / 2 : -den / 2)) / den;
            *ref[i] = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
        }
        double t1 = tnow();
        psx_gpu_draw_list(g, ot);
        double t2 = tnow();
        capture(g, k, den, interval);
        double t3 = tnow();
        memcpy(g, &saved, sizeof saved);
        tm_draw += t2 - t1; tm_cap += t3 - t2; tm_copy += tnow() - t3;
    }
    for (int i = 0; i < nref; i++) *ref[i] = orig[i];
    if (getenv("PSX_GPU_DUMP") && tm_n == 50) {   /* banco de pruebas: RAM + GPU + lista de este paso */
        FILE *f = fopen(getenv("PSX_GPU_DUMP"), "wb");
        if (f) { fwrite(&ot, 4, 1, f); fwrite(g, sizeof *g, 1, f); fwrite(psx_ram, 1, 0x200000, f); fclose(f); }
    }
    double t4 = tnow();
    uint32_t px0 = g->pixels_drawn, pr0 = g->prims_drawn;
    psx_gpu_draw_list(g, ot);
    if (tm_n == 99 && getenv("PSX_INTERP_DBG")) fprintf(stderr, "interp: imagen real: %u primitivas, %u pixeles\n", g->prims_drawn - pr0, g->pixels_drawn - px0);
    tm_real += tnow() - t4;
    capture(g, den, den, interval);
    if (++tm_n == 100 && getenv("PSX_INTERP_DBG")) {
        fprintf(stderr, "interp: por paso (ms): emparejar %.2f, dibujar intermedias %.2f, capturar %.2f, copiar estado %.2f, dibujo real %.2f\n",
                tm_pair * 10, tm_draw * 10, tm_cap * 10, tm_copy * 10, tm_real * 10);
        tm_n = 0; tm_pair = tm_draw = tm_cap = tm_copy = tm_real = 0;
    }
    return 1;
}

void psx_interp_stats(int *gte, int *matched, int *paired, int *dragged, int *total) {
    *gte = st_gte; *matched = st_matched; *paired = st_paired; *dragged = st_dragged; *total = st_total;
}
