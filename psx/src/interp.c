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
#define JUMP 48               /* pixeles: un vertice que salta mas no se interpola... */
#define JUMP_FIT 128          /* ...salvo que cuadre con el movimiento global (giros rapidos) */
#define BACKDROP 128          /* primitiva sin GTE en el espacio 3D de este tamano: fondo (cielo) */

int psx_interp_on;
static int g_nfrac, g_min_vb;
static PsxInterpSink g_sink;

/* vertices de este paso y del anterior: clave del modelo (x, y, z) y posicion de pantalla */
typedef struct { uint64_t base; uint32_t sxy; int next; } Vert;
static Vert cur[MAXV], prv[MAXV];
static int ncur, nprv;
static int16_t pdx[MAXV], pdy[MAXV];
static uint8_t pok[MAXV];
static float lsx[MAXV], lsy[MAXV];      /* prediccion local (vecinos emparejados) */
static uint16_t lc[MAXV];
static int refk[131072];               /* vertice de la GTE de cada coordenada de la lista */

/* tablas hash con generacion (se "vacian" subiendo la generacion) */
typedef struct { uint64_t key; uint32_t val; uint32_t cnt; uint32_t gen; } Slot;
static Slot occ[HSIZE], prevt[HSIZE], valt[HSIZE], dragt[HSIZE];
static uint32_t occ_gen = 1, prev_gen = 1, val_gen = 1, drag_gen = 1;
static int32_t dsx[HSIZE], dsy[HSIZE];   /* sumas por punto de pantalla (dragt) */
static Slot pocc[HSIZE], pprimt[HSIZE];  /* apariciones de cada clave; clave -> primitiva anterior */
static uint32_t pocc_gen = 1, pprim_gen = 1;
static uint32_t pp_xy[MAXREF];           /* coordenadas de las primitivas del paso anterior */
static int pp_first[MAXREF], pp_n[MAXREF];
static int have_prev, last_vb = -1;
static int st_dup, st_gte, st_matched, st_paired, st_dragged, st_total, st_backdrop;

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

static int ref_ofs[MAXREF], cur_ofs;   /* desplazamiento de dibujo (E5) de cada coordenada (depuracion) */
static void add_ref(uint32_t *w) { if (nref < MAXREF) { ref[nref] = w; ref_prim[nref] = nprim; ref_ofs[nref] = cur_ofs; nref++; } }

/* identidad de cada primitiva para reconocerla en el paso anterior sin la GTE: comando,
 * desplazamiento de dibujo, pagina y CLUT de la textura (no el color: el del fondo cambia con la
 * orientacion), y la n-esima aparicion de esa clave en la lista */
static uint64_t prim_key[MAXREF], prim_okey[MAXREF];
static int prim_first[MAXREF], prim_start;
static uint8_t prim_poly[MAXREF];
static void end_prim(uint64_t k, int poly) {
    if (nprim < MAXREF) { prim_key[nprim] = k ^ ((uint64_t)cur_ofs << 40); prim_first[nprim] = prim_start; prim_poly[nprim] = (uint8_t)poly; }
    nprim++;
    prim_start = nref;
}

static void walk_packet(uint32_t *p, int n) {
    int i = 0;
    while (i < n) {
        uint32_t c = p[i] >> 24;
        if (c >= 0x20 && c < 0x40) {                       /* poligono */
            int tex = c & 4, gour = c & 0x10, nv = (c & 8) ? 4 : 3;
            int len = 1 + nv * (1 + (tex ? 1 : 0)) + (gour ? nv - 1 : 0);
            if (i + len > n) return;
            int k = i + 1;
            uint64_t key = c;
            for (int v = 0; v < nv; v++) {
                if (gour && v > 0) k++;
                add_ref(&p[k++]);
                if (tex) { if (v < 2) key = key << 16 ^ (p[k] >> 16); k++; }   /* CLUT, pagina */
            }
            end_prim(key, 1);
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
                end_prim(c, 0);
                i = k + 1;
            } else {
                int len = gour ? 4 : 3;
                if (i + len > n) return;
                add_ref(&p[i + 1]);
                add_ref(&p[i + (gour ? 3 : 2)]);
                end_prim(c, 0);
                i += len;
            }
        } else if (c >= 0x60 && c < 0x80) {                /* rectangulo */
            int len = 2 + ((c & 4) ? 1 : 0) + (((c >> 3) & 3) == 0 ? 1 : 0);
            if (i + len > n) return;
            add_ref(&p[i + 1]);
            end_prim((uint64_t)c << 16 ^ ((c & 4) ? p[i + 2] >> 16 : 0), 0);
            i += len;
        } else if (c == 0x02) i += 3;
        else if (c >= 0x80 && c < 0xA0) i += 4;
        else if (c >= 0xA0 && c < 0xC0) {
            if (i + 3 > n) return;
            uint32_t wh = p[i + 2];
            int words = (int)(((wh & 0xFFFF) * (wh >> 16) + 1) / 2);
            i += 3 + words;
        } else if (c >= 0xC0 && c < 0xE0) i += 3;
        else { if (c == 0xE5) cur_ofs = (int)(p[i] & 0x3FFFFF); i += 1; }
    }
}

static void walk(uint32_t addr) {
    nref = nprim = prim_start = 0;
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

/* base de las intermedias: la imagen real del paso en el area de dibujo. El juego no borra la
 * pantalla (isbg = 0 y sin rellenos en la lista): un fondo opaco la cubre entera. Si una
 * intermedia deja algun hueco (lo que el juego no dibujo por estar fuera de la pantalla, al
 * desplazar mucho la escena), se ve la imagen real y no memoria de video antigua o negro */
static void paste_rect(uint16_t *dst, const uint16_t *src, int x0, int y0, int w, int h) {
    for (int y = 0; y < h; y++) {
        int row = ((y0 + y) & (PSX_VRAM_H - 1)) * PSX_VRAM_W;
        if (x0 >= 0 && x0 + w <= PSX_VRAM_W) memcpy(dst + row + x0, src + row + x0, (size_t)w * 2);
        else for (int x = 0; x < w; x++) dst[row + ((x0 + x) & (PSX_VRAM_W - 1))] = src[row + ((x0 + x) & (PSX_VRAM_W - 1))];
    }
}

static double g_tstep;   /* hora del paso en curso (CLOCK_MONOTONIC, s) */

static void capture(PsxGpu *g, int num, int den, int vb) {
    int w, h;
    psx_display_frame_at(g->area_x1, g->area_y1, img, 1024 * 512, &w, &h);
    if (w && h) g_sink(img, w, h, num, den, vb, g_tstep);
}

/* ---- posiciones de una fase. La lista lleva, por primitiva con algo que mover, todas sus
 * coordenadas (direccion PS1 de la palabra, valor real y desplazamiento hacia el paso
 * anterior). La comparten el modo en el hilo del juego y los hilos de dibujo ---- */
typedef struct {
    int n, np;
    uint32_t addr[MAXREF], orig[MAXREF];
    int16_t dx[MAXREF], dy[MAXREF];
    int pstart[MAXREF + 1];
    uint8_t poly[MAXREF];
} PhaseList;

static int lerp(int o, int d, int k, int den) {
    int v = o + (d * (den - k) + (d >= 0 ? den / 2 : -den / 2)) / den;
    return v < -1024 ? -1024 : v > 1023 ? 1023 : v;   /* coordenadas de 11 bits de la GPU */
}

/* doble del area con signo (cuadrilatero en el orden de la PS1: 0, 1, 3, 2) */
static long area2(const int *x, const int *y, int nv) {
    static const int Q[4] = {0, 1, 3, 2};
    long a = 0;
    for (int i = 0; i < nv; i++) {
        int p = nv == 4 ? Q[i] : i, q = nv == 4 ? Q[(i + 1) & 3] : (i + 1) % nv;
        a += (long)x[p] * y[q] - (long)x[q] * y[p];
    }
    return a;
}

/* escribe las coordenadas de la fase k/den en (ram, scratch). Un poligono que en la intermedia
 * estaria de espaldas, o crece mucho mas que en los dos pasos reales, no se dibuja (se colapsa
 * en un punto). Son caras que se dan la vuelta durante el paso (laderas en la silueta del
 * terreno): al mover cada vertice en linea recta se quedan de cara antes de tiempo y ensenan
 * su textura por encima del fondo (puntos oscuros en el terreno lejano al girar rapido) */
static int st_hidden;
static void apply_phase(const PhaseList *L, int k, int den, uint8_t *ram, uint8_t *scratch) {
    for (int p = 0; p < L->np; p++) {
        int s0 = L->pstart[p], s1 = L->pstart[p + 1], nv = s1 - s0;
        int nx[4], ny[4], hide = 0;
        if (L->poly[p] && (nv == 3 || nv == 4)) {
            int ox[4], oy[4], px[4], py[4];
            for (int v = 0; v < nv; v++) {
                int i = s0 + v;
                ox[v] = (int16_t)(L->orig[i] & 0xFFFF); oy[v] = (int16_t)(L->orig[i] >> 16);
                px[v] = ox[v] + L->dx[i]; py[v] = oy[v] + L->dy[i];
                nx[v] = lerp(ox[v], L->dx[i], k, den); ny[v] = lerp(oy[v], L->dy[i], k, den);
            }
            long ak = area2(ox, oy, nv), a0 = area2(px, py, nv), at = area2(nx, ny, nv);
            long amax = labs(ak) > labs(a0) ? labs(ak) : labs(a0);
            /* de espaldas en el paso anterior (el juego no lo dibujaba) y de cara en este: se da
             * la vuelta hacia t* = |a0| / (|a0| + |ak|) del paso (aproximando el area lineal en
             * t); antes sigue de espaldas aunque la mezcla de vertices lo deje de cara */
            if (a0 && ak && (a0 > 0) != (ak > 0) && (long)k * (labs(a0) + labs(ak)) < (long)den * labs(a0)) hide = 1;
            else if (labs(at) > 2 * amax + 32 || (ak && (at > 0) != (ak > 0) && labs(at) > 16)) hide = 1;
        }
        if (hide) st_hidden++;
        for (int v = 0; v < nv; v++) {
            int i = s0 + v;
            uint32_t a = L->addr[i];
            uint32_t *w = (uint32_t *)((a & 0xFFFFFC00u) == PSX_SCRATCH_ADDR ? scratch + (a & 0x3FC) : ram + (a & (PSX_RAM_SIZE - 4)));
            int x, y;
            if (hide) { x = nx[0]; y = ny[0]; }
            else if (nv <= 4 && L->poly[p] && nv >= 3) { x = nx[v]; y = ny[v]; }
            else { x = lerp((int16_t)(L->orig[i] & 0xFFFF), L->dx[i], k, den); y = lerp((int16_t)(L->orig[i] >> 16), L->dy[i], k, den); }
            *w = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
        }
    }
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
    PhaseList L;
    int den, interval, x0, y0, dw, dh, on, rgb24;
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
            apply_phase(&J.L, k, den, ram, scratch);
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

/* la lista de la fase a partir del emparejamiento de este paso */
static PhaseList g_L;
static void build_list(PhaseList *L) {
    int n = 0, np = 0;
    for (int a = 0; a < nref;) {
        int b = a, any = 0;
        while (b < nref && ref_prim[b] == ref_prim[a]) { if (moved[b] && (ddx[b] || ddy[b])) any = 1; b++; }
        if (any) {
            L->pstart[np] = n;
            L->poly[np] = ref_prim[a] < MAXREF ? prim_poly[ref_prim[a]] : 0;
            np++;
            for (int i = a; i < b; i++) {
                L->addr[n] = psx_addr(ref[i]); L->orig[n] = orig[i];
                L->dx[n] = moved[i] ? ddx[i] : 0; L->dy[n] = moved[i] ? ddy[i] : 0;
                n++;
            }
        }
        a = b;
    }
    L->pstart[np] = n;
    L->n = n; L->np = np;
}

int psx_interp_skip_test;   /* prueba: S pasos de cada S + 1 se dibujan sin usarse de referencia */

int psx_interp_draw(void *gpu, uint32_t ot) {
    PsxGpu *g = gpu;
    static int parity;
    int ph = psx_interp_skip_test ? parity++ % (psx_interp_skip_test + 1) : 0;
    if (ph) {   /* pasos k+1 .. k+S: solo la imagen real (fase ph/0) */
        psx_gpu_draw_list(g, ot);
        capture(g, ph, 0, 1);
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
    pocc_gen++;
    for (int pi = 0; pi < nprim && pi < MAXREF; pi++) {
        Slot *o = slot(pocc, pocc_gen, mix(prim_key[pi]), 1);
        uint32_t n = o ? o->cnt++ : 0;
        prim_okey[pi] = mix(prim_key[pi] ^ ((uint64_t)n << 52) ^ 0x5bd1e995u);
    }
    if (getenv("PSX_INTERP_DBG")) {
        static int c;
        if (c++ % 50 == 0) fprintf(stderr, "interp: %d vertices, %d repetidos (misma x,y,z del modelo)\n", ncur, st_dup);
    }
    st_dup = 0; st_backdrop = 0;
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
                if (fabs(dx) > JUMP_FIT || fabs(dy) > JUMP_FIT) continue;
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
    /* 2. pareja de cada vertice. Primero las claves unicas en los dos pasos (con la prediccion
     *    del movimiento global solo para admitir saltos grandes). Luego las repetidas (casillas
     *    del terreno con coordenadas locales): el candidato mas cercano a la prediccion de sus
     *    vecinos, la media de los vertices unicos de sus mismos poligonos (al lado en 3D); el
     *    movimiento global solo si no tiene vecinos emparejados. Con solo el global, en giros
     *    fuertes se elegia el candidato equivocado, la malla se revolvia en la silueta y
     *    asomaban caras de detras (puntos oscuros en el terreno lejano, Retroid 1.3) */
    for (int i = 0; i < nref; i++) {   /* vertice de la GTE de cada coordenada de la lista */
        Slot *sv = slot(valt, val_gen, mix(*ref[i] | (1ull << 40)), 0);
        refk[i] = sv && sv->val ? (int)sv->val - 1 : -1;
    }
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {   /* predicciones locales para los que faltan */
            for (int i = 0; i < ncur; i++) lc[i] = 0;
            for (int a = 0; a < nref;) {
                int b = a, n = 0, sx = 0, sy = 0;
                while (b < nref && ref_prim[b] == ref_prim[a]) {
                    int k = refk[b];
                    if (k >= 0 && pok[k]) { sx += pdx[k]; sy += pdy[k]; n++; }
                    b++;
                }
                if (n)
                    for (int i = a; i < b; i++) {
                        int k = refk[i];
                        if (k < 0 || pok[k]) continue;
                        if (!lc[k]) lsx[k] = lsy[k] = 0;
                        lsx[k] += (float)sx / n; lsy[k] += (float)sy / n; lc[k]++;
                    }
                a = b;
            }
        }
        for (int i = 0; i < ncur; i++) {
            if (pass == 0) pok[i] = 0;
            else if (pok[i]) continue;
            if (!have_prev) continue;
            Slot *p = slot(prevt, prev_gen, cur[i].base, 0);
            if (!p) continue;
            Slot *o = slot(occ, occ_gen, cur[i].base, 0);
            int uniq = p->cnt == 1 && o && o->cnt == 1;
            if (pass == 0 && !uniq) continue;
            int x1 = lo(cur[i].sxy), y1 = hi(cur[i].sxy);
            double ex = 0, ey = 0, tol = 0;
            int pred = 0;
            if (pass == 1 && lc[i]) { ex = lsx[i] / lc[i]; ey = lsy[i] / lc[i]; tol = 8; pred = 1; }
            else if (model) {
                ex = A[0][0] * (x1 - 160) + A[0][1] * (y1 - 128) + A[0][2];
                ey = A[1][0] * (x1 - 160) + A[1][1] * (y1 - 128) + A[1][2];
                tol = 12; pred = 1;
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
            if (abs(dx) > JUMP_FIT || abs(dy) > JUMP_FIT) continue;
            int big = abs(dx) > JUMP || abs(dy) > JUMP;
            if (pred ? bd > tol * tol && (pass == 1 || big) : big) continue;
            pdx[i] = (int16_t)dx; pdy[i] = (int16_t)dy; pok[i] = 1;
        }
    }
    /* 3. desplazamiento de cada coordenada de la lista */
    for (int i = 0; i < nref; i++) {
        uint32_t w = *ref[i];
        orig[i] = w;
        ddx[i] = ddy[i] = 0;
        moved[i] = 0;
        int k = refk[i];
        if (k < 0) continue;
        st_matched++;
        moved[i] = 1;
        if (!pok[k]) continue;
        ddx[i] = pdx[k]; ddy[i] = pdy[k];
        moved[i] = 2;
        st_paired++;
    }
    /* sin pareja, en primitivas de la GTE: la media de las emparejadas de la primitiva, o si no
     * hay ninguna, el movimiento global. Un mismo punto de pantalla puede estar en varias
     * primitivas (vertices compartidos del terreno): su desplazamiento es la media de lo que
     * proponen todas, igual en cada una. Con un valor por primitiva, los vertices compartidos
     * se separaban y asomaba el fondo entre los poligonos (grietas negras al girar rapido) */
    drag_gen++;
    int ofs3d = -1;   /* desplazamiento de dibujo (E5) de lo que sale de la GTE: el espacio 3D */
    for (int i = 0; i < nref && ofs3d < 0; i++) if (moved[i]) ofs3d = ref_ofs[i];
    /* bordes del area de dibujo en coordenadas de ese espacio */
    int o3x = ((ofs3d & 0x7FF) ^ 0x400) - 0x400, o3y = (((ofs3d >> 11) & 0x7FF) ^ 0x400) - 0x400;
    int bx0 = g->area_x1 - o3x, bx1 = g->area_x2 - o3x, by0 = g->area_y1 - o3y, by1 = g->area_y2 - o3y;
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
                int px, py;
                if (np) { px = sx / np; py = sy / np; }
                else if (model) {
                    int x1 = lo(orig[i]), y1 = hi(orig[i]);
                    px = (int)lrint(A[0][0] * (x1 - 160) + A[0][1] * (y1 - 128) + A[0][2]);
                    py = (int)lrint(A[1][0] * (x1 - 160) + A[1][1] * (y1 - 128) + A[1][2]);
                } else continue;
                Slot *d = slot(dragt, drag_gen, mix(orig[i] | (2ull << 40)), 1);
                if (!d) continue;
                if (!d->cnt) { dsx[d - dragt] = 0; dsy[d - dragt] = 0; }
                dsx[d - dragt] += px; dsy[d - dragt] += py; d->cnt++;
                moved[i] = 3; st_dragged++;
            }
        else if (ref_ofs[a] == ofs3d) {
            /* sin nada de la GTE pero en el espacio 3D y grande: el fondo (cielo, bruma) que el
             * juego coloca con la CPU segun la orientacion de la camara. Se interpola entre sus
             * propios vertices de los dos pasos si se reconoce la misma primitiva en el anterior
             * (prim_okey) y si no, con el movimiento global. Si se quedara quieto, o se moviera
             * distinto que el terreno, al girar rapido asomaria su parte baja
             * bajo el terreno interpolado (terreno lejano negro, Retroid 1.3). Los sprites
             * pequenos (llamas, explosiones) y el HUD (otro desplazamiento) no se tocan */
            int x0 = 1 << 30, x1 = -(1 << 30), y0 = 1 << 30, y1 = -(1 << 30);
            for (int i = a; i < b; i++) {
                int x = lo(orig[i]), y = hi(orig[i]);
                if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
            }
            int pi = ref_prim[a], exact = 0;
            Slot *ps = (b - a >= 3 && (x1 - x0 >= BACKDROP || y1 - y0 >= BACKDROP) && pi < MAXREF)
                           ? slot(pprimt, pprim_gen, prim_okey[pi], 0) : NULL;
            if (ps && pp_n[ps->val - 1] == b - a) {
                /* la misma primitiva en el paso anterior: sus propios vertices (el movimiento
                 * exacto que le dio el juego) */
                int q = pp_first[ps->val - 1];
                exact = 1;
                for (int i = a; i < b; i++) {
                    int dx = lo(pp_xy[q + i - a]) - lo(orig[i]), dy = hi(pp_xy[q + i - a]) - hi(orig[i]);
                    if (abs(dx) > JUMP_FIT || abs(dy) > JUMP_FIT) exact = 0;
                }
                if (exact)
                    for (int i = a; i < b; i++) {
                        int px = lo(pp_xy[q + i - a]) - lo(orig[i]), py = hi(pp_xy[q + i - a]) - hi(orig[i]);
                        Slot *d = slot(dragt, drag_gen, mix(orig[i] | (2ull << 40)), 1);
                        if (!d) continue;
                        if (!d->cnt) { dsx[d - dragt] = 0; dsy[d - dragt] = 0; }
                        dsx[d - dragt] += px; dsy[d - dragt] += py; d->cnt++;
                        moved[i] = 3; st_backdrop++;
                    }
            }
            if (!exact && model && b - a >= 3 && (x1 - x0 >= BACKDROP || y1 - y0 >= BACKDROP))
                for (int i = a; i < b; i++) {
                    int x = lo(orig[i]), y = hi(orig[i]);
                    int px = (int)lrint(A[0][0] * (x - 160) + A[0][1] * (y - 128) + A[0][2]);
                    int py = (int)lrint(A[1][0] * (x - 160) + A[1][1] * (y - 128) + A[1][2]);
                    /* en el borde de la pantalla o fuera: solo a lo largo del borde, para que
                     * el fondo siga cubriendola entera (si no, franjas negras en los bordes) */
                    if (x <= bx0 + 1 || x >= bx1 - 1) px = 0;
                    if (y <= by0 + 1 || y >= by1 - 1) py = 0;
                    Slot *d = slot(dragt, drag_gen, mix(orig[i] | (2ull << 40)), 1);
                    if (!d) continue;
                    if (!d->cnt) { dsx[d - dragt] = 0; dsy[d - dragt] = 0; }
                    dsx[d - dragt] += px; dsy[d - dragt] += py; d->cnt++;
                    moved[i] = 3; st_backdrop++;
                }
        }
        a = b;
    }
    for (int i = 0; i < nref; i++) {
        if (moved[i] != 3) continue;
        Slot *d = slot(dragt, drag_gen, mix(orig[i] | (2ull << 40)), 0);
        if (!d || !d->cnt) { ddx[i] = ddy[i] = 0; continue; }
        int n = (int)d->cnt, x = dsx[d - dragt], y = dsy[d - dragt];
        ddx[i] = (int16_t)((x >= 0 ? x + n / 2 : x - n / 2) / n);
        ddy[i] = (int16_t)((y >= 0 ? y + n / 2 : y - n / 2) / n);
    }
    int dump_now = getenv("PSX_INTERP_PRIMDUMP") && (getenv("PSX_INTERP_PROBE") ? abs(VSync(-1) - atoi(getenv("PSX_INTERP_PROBE"))) <= 1 : tm_n == 50);
    if (dump_now) {   /* depuracion: primitiva, coordenada, estado */
        FILE *f = fopen(getenv("PSX_INTERP_PRIMDUMP"), "w");
        if (f) {
            for (int i = 0; i < nref; i++) fprintf(f, "%d %d %d %d %d %d %d %d\n", ref_prim[i], lo(orig[i]), hi(orig[i]), moved[i], ddx[i], ddy[i],
                                              ((ref_ofs[i] & 0x7FF) ^ 0x400) - 0x400, (((ref_ofs[i] >> 11) & 0x7FF) ^ 0x400) - 0x400);
            fclose(f);
        }
    }
    /* el siguiente paso se compara con este: listas por clave */
    pprim_gen++;
    for (int pi = 0; pi < nprim && pi < MAXREF; pi++) {
        Slot *t = slot(pprimt, pprim_gen, prim_okey[pi], 1);
        if (!t) continue;
        t->val = (uint32_t)pi + 1;
        pp_first[pi] = prim_first[pi];
        pp_n[pi] = (pi + 1 < nprim ? prim_first[pi + 1] : nref) - prim_first[pi];
    }
    memcpy(pp_xy, orig, (size_t)nref * sizeof orig[0]);
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
            build_list(&J.L);
            J.ot = ot; J.den = g_nfrac + 1; J.interval = interval; J.t = ta; J.sink = g_sink;
            J.x0 = g->area_x1; J.y0 = g->area_y1;
            psx_display_geom(&J.dw, &J.dh, &J.on, &J.rgb24);
            if (J.dw * J.dh > 1024 * 512) J.dw = J.dh = 0;
        }
        double tc = tnow();
        psx_gpu_draw_list(g, ot);
        capture(g, g_nfrac + 1, g_nfrac + 1, interval);
        if (!busy && J.dw > 0 && J.dh > 0) {
            paste_rect(J.gpu.vram, g->vram, J.x0, J.y0, J.dw, J.dh);
            pthread_mutex_lock(&J.mu);
            J.pending = J.nthreads; J.seq++; J.jobs++;
            pthread_cond_broadcast(&J.go);
            pthread_mutex_unlock(&J.mu);
        }
        pthread_mutex_lock(&J.mu);
        J.snap_s += tc - tb;
        J.real_s += tnow() - tc;
        pthread_mutex_unlock(&J.mu);
        return 1;
    }
    /* imagenes intermedias: la lista con las posiciones mezcladas, sobre una copia del estado */
    double tb = tnow(); tm_pair += tb - ta;
    J.steps++;   /* sin hilos de dibujo: solo este hilo toca J */
    static PsxGpu saved, real;
    build_list(&g_L);
    memcpy(&saved, g, sizeof saved);
    int ax = g->area_x1, ay = g->area_y1, dw, dh, don, d24;
    psx_display_geom(&dw, &dh, &don, &d24);
    if (dw * dh > 1024 * 512) dw = dh = 0;
    tm_copy += tnow() - tb;
    double t4 = tnow();
    uint32_t px0 = g->pixels_drawn, pr0 = g->prims_drawn;
    psx_gpu_draw_list(g, ot);   /* la real primero: es la base de las intermedias */
    if (tm_n == 99 && getenv("PSX_INTERP_DBG")) fprintf(stderr, "interp: imagen real: %u primitivas, %u pixeles\n", g->prims_drawn - pr0, g->pixels_drawn - px0);
    tm_real += tnow() - t4;
    memcpy(&real, g, sizeof real);
    int den = g_nfrac + 1;
    for (int k = 1; k <= g_nfrac; k++) {
        memcpy(g, &saved, sizeof saved);
        paste_rect(g->vram, real.vram, ax, ay, dw, dh);
        apply_phase(&g_L, k, den, psx_ram, psx_scratch);
        double t1 = tnow();
        int pvb, px_, py_;
        if (getenv("PSX_INTERP_PROBE") && sscanf(getenv("PSX_INTERP_PROBE"), "%d,%d,%d", &pvb, &px_, &py_) == 3 && abs(VSync(-1) - pvb) <= 1) {
            /* depuracion: que paquetes de la lista escriben el pixel (px_, py_) de la intermedia */
            uint16_t *pix = &g->vram[(ay + py_) * PSX_VRAM_W + ax + px_];
            uint32_t a = ot;
            int idx = 0;
            fprintf(stderr, "sonda vb %d k %d: pixel base %04x\n", VSync(-1), k, *pix);
            while ((a & 0xFFFFFF) != 0xFFFFFF && idx < 200000) {
                const uint32_t *pk = (const uint32_t *)psx_ptr(a);
                uint16_t before = *pix;
                for (int w = 1; w <= (int)(pk[0] >> 24); w++) psx_gpu_gp0(g, pk[w]);
                if (*pix != before) {
                    fprintf(stderr, "sonda: paquete %d (%08x):", idx, a);
                    for (int w = 1; w <= (int)(pk[0] >> 24) && w <= 12; w++) fprintf(stderr, " %08x", pk[w]);
                    fprintf(stderr, " -> %04x\n", *pix);
                }
                a = 0x80000000u | (pk[0] & 0xFFFFFF);
                idx++;
            }
        } else
        psx_gpu_draw_list(g, ot);
        double t2 = tnow();
        capture(g, k, den, interval);
        double t3 = tnow();
        tm_draw += t2 - t1; tm_cap += t3 - t2;
    }
    for (int i = 0; i < nref; i++) *ref[i] = orig[i];
    if (getenv("PSX_GPU_DUMP") && tm_n == 50) {   /* banco de pruebas: RAM + GPU (antes del dibujo) + lista */
        FILE *f = fopen(getenv("PSX_GPU_DUMP"), "wb");
        if (f) { fwrite(&ot, 4, 1, f); fwrite(&saved, sizeof saved, 1, f); fwrite(psx_ram, 1, 0x200000, f); fclose(f); }
    }
    double t5 = tnow();
    memcpy(g, &real, sizeof real);
    tm_copy += tnow() - t5;
    capture(g, den, den, interval);
    if (getenv("PSX_INTERP_PRIMDUMP") && tm_n == 50) {   /* depuracion: la imagen real del paso volcado */
        char path[600];
        int w, h;
        snprintf(path, sizeof path, "%s.ppm", getenv("PSX_INTERP_PRIMDUMP"));
        psx_display_frame_at(g->area_x1, g->area_y1, img, 1024 * 512, &w, &h);
        FILE *f = fopen(path, "wb");
        if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h); for (int i = 0; i < w * h; i++) fwrite(&img[i], 1, 3, f); fclose(f); }
    }
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
