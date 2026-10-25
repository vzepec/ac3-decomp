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
#include "psx_libgte.h"
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
typedef struct { uint64_t base; uint32_t sxy; int next; uint32_t grp; int16_t vx, vy, vz; uint16_t mat; } Vert;   /* grp: el objeto (matriz de la GTE) */
static Vert cur[MAXV], prv[MAXV];
static int ncur, nprv;
/* etapa 8, paso 3b: la matriz de cada pieza (registros de control de la GTE con que se proyecto:
 * rotacion c0-c4, traslacion c5-c7, centro y distancia c24-c26), para interpolarla entera */
#define MAXM 16384
typedef struct { int32_t c[8], ofx, ofy, h; uint32_t grp; } Mat;
static Mat mats[MAXM], pmats[MAXM];
static int nmat, npmat;
static int16_t pdx[MAXV], pdy[MAXV];
static uint8_t pok[MAXV];
static int pbest[MAXV];                 /* vertice del paso anterior con que se emparejo */
static float lsx[MAXV], lsy[MAXV];      /* prediccion local (vecinos emparejados) */
static uint16_t lc[MAXV];
static uint8_t in3d[MAXV];              /* vertice usado en el espacio 3D */
static int refk[131072];               /* vertice de la GTE de cada coordenada de la lista */

/* tablas hash con generacion (se "vacian" subiendo la generacion) */
typedef struct { uint64_t key; uint32_t val; uint32_t cnt; uint32_t gen; } Slot;
static Slot occ[HSIZE], prevt[HSIZE], valt[HSIZE], dragt[HSIZE], occ3[HSIZE];
static uint32_t occ3_gen = 1;
static uint32_t occ_gen = 1, prev_gen = 1, val_gen = 1, drag_gen = 1;
static int32_t dsx[HSIZE], dsy[HSIZE];   /* sumas por punto de pantalla (dragt) */
static Slot pocc[HSIZE], pprimt[HSIZE];  /* apariciones de cada clave; clave -> primitiva anterior */
static uint32_t pocc_gen = 1, pprim_gen = 1;
static uint32_t pp_xy[MAXREF];           /* coordenadas de las primitivas del paso anterior */
static int pp_first[MAXREF], pp_n[MAXREF];
static int have_prev, last_vb = -1;
static long st_stretch, st_stretch_steps;
static int st_reject, g_cut;
static long st_cuts, st_rejects;
static void stretch_summary(void) {
    fprintf(stderr, "estirados: %ld poligonos en %ld pasos (corregidos: se mueven enteros); parejas descartadas %ld; cortes de camara %ld\n",
            st_stretch, st_stretch_steps, st_rejects, st_cuts);
}
static int st_dup, st_gte, st_matched, st_paired, st_dragged, st_total, st_backdrop, st_stuck;

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

void psx_interp_vertex(int32_t x, int32_t y, int32_t z, int32_t sx, int32_t sy, uint32_t grp) {
    if (ncur >= MAXV) return;
    uint64_t base = mix(((uint64_t)(uint16_t)x) | ((uint64_t)(uint16_t)y << 16) | ((uint64_t)(uint16_t)z << 32) | (1ull << 48));
    Slot *o = slot(occ, occ_gen, base, 1);
    if (o) { if (o->cnt) st_dup++; o->cnt++; }
    cur[ncur].base = base;
    cur[ncur].sxy = ((uint32_t)(uint16_t)sy << 16) | (uint16_t)sx;
    cur[ncur].grp = grp;
    cur[ncur].vx = (int16_t)x; cur[ncur].vy = (int16_t)y; cur[ncur].vz = (int16_t)z;
    const PsxGte *g = psx_gte_hook_ctx ? psx_gte_hook_ctx : (const PsxGte *)psx_gte_get_context();
    if (!nmat || mats[nmat - 1].grp != grp || mats[nmat - 1].ofx != g->c[24] || mats[nmat - 1].ofy != g->c[25] || mats[nmat - 1].h != g->c[26]) {
        /* la pieza cambia (otra matriz u otra proyeccion): su matriz */
        if (nmat < MAXM) {
            Mat *m = &mats[nmat++];
            memcpy(m->c, g->c, sizeof m->c);
            m->ofx = g->c[24]; m->ofy = g->c[25]; m->h = g->c[26]; m->grp = grp;
        }
    }
    cur[ncur].mat = (uint16_t)(nmat ? nmat - 1 : 0);
    ncur++;
}

/* ---- recorrido de la lista: palabras de coordenadas de pantalla de poligonos, lineas y
 * rectangulos, agrupadas por primitiva ---- */
static uint32_t *ref[MAXREF];
static int ref_prim[MAXREF], nref, nprim;

/* espacio de dibujo de cada coordenada: desplazamiento (E5) y origen del area de dibujo (E3).
 * La escena 3D, el radar y el mapa se distinguen por estos dos */
static int ref_ofs[MAXREF], cur_ofs, cur_area;
static int64_t ref_spc[MAXREF];
static void add_ref(uint32_t *w) {
    if (nref < MAXREF) { ref[nref] = w; ref_prim[nref] = nprim; ref_ofs[nref] = cur_ofs; ref_spc[nref] = (int64_t)cur_ofs | (int64_t)cur_area << 22; nref++; }
}

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
        else { if (c == 0xE5) cur_ofs = (int)(p[i] & 0x3FFFFF); if (c == 0xE3) cur_area = (int)(p[i] & 0xFFFFF); i += 1; }
    }
}

/* ofs0 / area0: el desplazamiento (E5) y el origen del area (E3) que la GPU tiene al empezar la
 * lista (los de PutDrawEnv). Con 0 (paso 19 y antes), lo que la lista dibuja antes de su primera
 * E3 (el cielo, al principio) quedaba en otro "espacio" que la escena 3D y no se interpolaba:
 * saltaba un paso entero mientras el terreno avanzaba a medias (el cielo temblaba al cabecear) */
static void walk(uint32_t addr, int ofs0, int area0) {
    nref = nprim = prim_start = 0;
    cur_ofs = ofs0; cur_area = area0;
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

/* ---- dibujo por GLES (paso 7, psx_interp_hw): en vez de la imagen se entrega la lista anotada
 * (psx_gpu_record) con la VRAM de antes de dibujarla; el anfitrion la dibuja a alta resolucion
 * (psx/gl/psx_hw.c). La imagen real se sigue dibujando por software (la VRAM del juego); las
 * intermedias, solo se interpretan (registro sin dibujar la imagen principal) ---- */
static PsxInterpHwSink g_hwsink;
static int g_hwfull, g_hwmain;
void psx_interp_hw(PsxInterpHwSink sink, int full) { g_hwsink = sink; g_hwfull = full; }
void psx_interp_hw_main(int on) { g_hwmain = on; }
static int g_real_only;   /* la imagen real de este paso solo se dibujo en la GPU */
static int g_real_nread;
static int16_t g_real_read[4][4];

static int hw_prepare(PsxHwList *l, int x1, int y1, int x2, int y2) {
    if (!l->p) {
        l->max = 65536; l->p = malloc((size_t)l->max * sizeof *l->p);
        l->maxdata = 1024 * 512; l->data = malloc((size_t)l->maxdata * 2);
        if (!l->p || !l->data) return -1;
    }
    l->n = l->ndata = 0;
    l->main_x1 = x1; l->main_y1 = y1; l->main_x2 = x2; l->main_y2 = y2;
    return 0;
}

/* la imagen real del paso: por software y, con GLES, ademas anotada y entregada */
double tm_raster_real, tm_raster_w; int tm_raster_nr, tm_raster_nw;
static double tnow(void);
static void draw_real(PsxGpu *g, uint32_t ot, int num, int den, int vb) {
    int w, h, on, rgb24;
    psx_display_geom(&w, &h, &on, &rgb24);
    static PsxHwList L;
    static uint16_t bg[1024 * 512];
    if (!g_hwsink || !on || rgb24 || w <= 0 || h <= 0 || w * h > 1024 * 512 ||
        hw_prepare(&L, g->area_x1, g->area_y1, g->area_x2, g->area_y2)) {
        double r0 = tnow();
        psx_gpu_draw_list(g, ot);
        tm_raster_real += tnow() - r0; tm_raster_nr++;
        capture(g, num, den, vb);
        return;
    }
    int x0 = g->area_x1, y0 = g->area_y1;
    memcpy(bg, g->vram, sizeof bg);
    /* etapa 8, paso 2: con g_hwmain, el software no dibuja la imagen (solo lo de fuera); la GPU
     * parte de su propia copia si el software ya no la tenia al dia */
    L.main_stale = psx_gpu_main_stale(g->area_x1, g->area_y1, g->area_x2, g->area_y2);
    g_real_only = g_hwmain && !g_hwfull &&
                  !psx_gpu_list_copies_from(ot, psx_ram, psx_scratch, g->area_x1, g->area_y1, g->area_x2, g->area_y2);
    double r0 = tnow();
    psx_gpu_record(&L, g_real_only);
    psx_gpu_draw_list(g, ot);
    tm_raster_real += tnow() - r0; tm_raster_nr++;
    g_real_nread = L.nread; memcpy(g_real_read, L.read, sizeof g_real_read);
    psx_gpu_record_flush(g);
    psx_gpu_record(NULL, 0);
    g_hwsink(&L, bg, g->vram, x0, y0, w, h, num, den, vb, g_tstep);
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
    uint8_t *ramk[8], *scrk[8];       /* etapa 8: la RAM de cada fase con el mundo redibujado */
    int rebuilt;
    int rr_async;                     /* etapa 8: el hilo 0 redibuja cada fase (psx_interp_rebuild_async) */
    int real_only;                    /* etapa 8, paso 2: la real solo esta en la GPU (base de las intermedias) */
    int nread;                        /* y las zonas de la GPU que lee como textura (las mismas en las intermedias) */
    int16_t read[4][4];
    PsxGpu gpu;
    PhaseList L;
    int den, interval, x0, y0, dw, dh, on, rgb24;
    uint32_t ot;
    double t;
    PsxInterpSink sink;
    PsxInterpHwSink hwsink;
    int ax1, ay1, ax2, ay2;   /* area principal (la de la imagen) */
    /* rendimiento */
    double worker_s, worker_max;
    int jobs, skipped, steps, unpaired;
    double pair_s, snap_s, real_s;
    double worker0_s, rr_s;
    int rr_n;
} J = {.mu = PTHREAD_MUTEX_INITIALIZER, .go = PTHREAD_COND_INITIALIZER, .done = PTHREAD_COND_INITIALIZER};

static double tnow(void);

/* etapa 8: redibujo del mundo estatico por fase (psx_interp_rebuild) */
static PsxInterpRebuild g_rebuild;
static const PsxInterpRebuildAsync *g_rra;
void psx_interp_rebuild(PsxInterpRebuild fn) { g_rebuild = fn; if (fn) g_rra = NULL; }
void psx_interp_rebuild_async(const PsxInterpRebuildAsync *a) { g_rra = a; if (a) g_rebuild = NULL; }
int psx_interp_rebuild_fails;   /* pasos en que no se pudo (corte de plano, sin datos) */
int psx_interp_feedback;        /* (paso 10) pasos sin intermedias: la lista lee la imagen como textura */
double psx_interp_rebuild_s;   /* tiempo total en el redibujo (medicion) */
int psx_interp_rebuild_n;
static int rebuild_phases(uint32_t ot, int den) {
    double t0 = tnow();
    static uint8_t ram[PSX_RAM_SIZE], scr[0x400];
    static PsxGte gte;
    PsxGte *gc = (PsxGte *)psx_gte_get_context();
    memcpy(ram, psx_ram, sizeof ram); memcpy(scr, psx_scratch, sizeof scr); gte = *gc;
    int ok = 1;
    for (int k = 1; k < den && ok; k++) {
        if (!J.ramk[k]) { J.ramk[k] = malloc(PSX_RAM_SIZE); J.scrk[k] = malloc(0x400); }
        if (!J.ramk[k] || !J.scrk[k]) { ok = 0; break; }
        /* fase k: la coordenada intermedia es la real + d * (den - k) / den, de modo que t = k / den */
        if (g_rebuild(ot, (float)k / (float)den)) ok = 0;
        else { memcpy(J.ramk[k], psx_ram, PSX_RAM_SIZE); memcpy(J.scrk[k], psx_scratch, 0x400); }
        memcpy(psx_ram, ram, sizeof ram); memcpy(psx_scratch, scr, sizeof scr); *gc = gte;
    }
    if (!ok) psx_interp_rebuild_fails++;
    psx_interp_rebuild_s += tnow() - t0;
    psx_interp_rebuild_n++;
    return ok;
}

static void *worker(void *arg) {
    int id = (int)(intptr_t)arg;
    if (J.on_start) J.on_start();
    uint8_t *ram = malloc(PSX_RAM_SIZE), scratch[0x400];
    PsxGpu *gpu = malloc(sizeof *gpu);
    uint32_t *out = malloc(1024 * 512 * 4);
    PsxHwList hl = {0};
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
        /* con el redibujo en este hilo (una sola copia del interprete), el hilo 0 hace todas las fases */
        int rra = J.rr_async, k0 = rra ? (id == 0 ? 1 : J.den) : id + 1, kstep = rra ? 1 : nt;
        for (int k = k0; k < J.den; k += kstep) {
            uint8_t *R = ram, *Sc = scratch;
            if (rra) {
                double tr = tnow();
                if (g_rra->build(J.ot, (float)k / (float)J.den, J.ram, J.scratch, &R, &Sc)) {
                    R = ram; Sc = scratch;
                    memcpy(ram, J.ram, PSX_RAM_SIZE); memcpy(scratch, J.scratch, sizeof scratch);
                    pthread_mutex_lock(&J.mu); psx_interp_rebuild_fails++; pthread_mutex_unlock(&J.mu);
                }
                double dr = tnow() - tr;
                pthread_mutex_lock(&J.mu); psx_interp_rebuild_s += dr; psx_interp_rebuild_n++; J.rr_s += dr; J.rr_n++; pthread_mutex_unlock(&J.mu);
            } else if (J.rebuilt) { memcpy(ram, J.ramk[k], PSX_RAM_SIZE); memcpy(scratch, J.scrk[k], sizeof scratch); }
            else if (!copied) { memcpy(ram, J.ram, PSX_RAM_SIZE); memcpy(scratch, J.scratch, sizeof scratch); copied = 1; }
            memcpy(gpu, &J.gpu, sizeof *gpu);
            int den = J.den;
            apply_phase(&J.L, k, den, R, Sc);
            if (J.hwsink && !hw_prepare(&hl, J.ax1, J.ay1, J.ax2, J.ay2)) {
                hl.main_stale = J.real_only;   /* la base (la real pegada) no esta en el software */
                /* GLES: solo interpretar (lo de fuera de la imagen, p. ej. paletas, si se dibuja). Salvo
                 * si la lista copia desde la imagen (el efecto de calor de los motores): entonces el
                 * software la dibuja entera, para que la copia lea lo de esta intermedia y no la real
                 * pegada debajo (con el HUD ya dibujado: salia una banda verde bajo las llamas) */
                static int nofix = -1;   /* prueba: PSX_INTERP_NOCOPYFIX=1, sin esto (para ver el fallo) */
                if (nofix < 0) nofix = getenv("PSX_INTERP_NOCOPYFIX") != NULL;
                int only = nofix || !psx_gpu_list_copies_from(J.ot, R, Sc, J.ax1, J.ay1, J.ax2, J.ay2);
                static _Thread_local PsxGpu *ref;   /* comparador (full): la referencia, dibujada entera en otra copia */
                if (g_hwfull) { if (!ref) ref = malloc(sizeof *ref); if (ref) memcpy(ref, gpu, sizeof *ref); }
                psx_gpu_record(&hl, only);
                psx_gpu_draw_list_mem(gpu, J.ot, R, Sc);
                psx_gpu_record_flush(gpu);
                psx_gpu_record(NULL, 0);
                if (g_hwfull && ref) psx_gpu_draw_list_mem(ref, J.ot, R, Sc);
                hl.nread = J.nread; memcpy(hl.read, J.read, sizeof hl.read);   /* lo que lee la real de la GPU */
                J.hwsink(&hl, J.gpu.vram, g_hwfull && ref ? ref->vram : NULL, J.x0, J.y0, J.dw, J.dh, k, den, J.interval, J.t);
                continue;
            }
            double r0 = tnow();
            psx_gpu_draw_list_mem(gpu, J.ot, R, Sc);
            if (id == 0) { tm_raster_w += tnow() - r0; tm_raster_nw++; }
            psx_display_convert(gpu->vram, J.x0, J.y0, J.dw, J.dh, J.on, J.rgb24, out);
            J.sink(out, J.dw, J.dh, k, den, J.interval, J.t);
        }
        if (rra && id == 0) g_rra->done();
        double dt = tnow() - t0;
        pthread_mutex_lock(&J.mu);
        J.worker_s += dt;
        if (id == 0) J.worker0_s += dt;
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
    p->worker0_ms = J.jobs ? J.worker0_s * 1000 / J.jobs : 0;
    p->rebuild_ms = J.rr_n ? J.rr_s * 1000 / J.rr_n : 0;
    if (reset) {
        J.steps = J.jobs = J.skipped = J.unpaired = J.rr_n = 0;
        J.pair_s = J.snap_s = J.real_s = J.worker_s = J.worker_max = J.worker0_s = J.rr_s = 0;
    }
    pthread_mutex_unlock(&J.mu);
}

static double tnow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double tm_pair, tm_draw, tm_cap, tm_copy, tm_real; static int tm_n;

/* movimiento global afin d = A * (P - centro) por minimos cuadrados, con un rechazo de los que
 * se apartan mas de 6 px del primer ajuste; 1 si hay al menos minn puntos */
static int fit_affine(const int16_t *px, const int16_t *py, const int16_t *qx, const int16_t *qy, int n, int minn, double A[2][3]) {
    double B[2][3] = {{0}};
    int ok = 0;
    for (int it = 0; it < 2; it++) {
        double M[3][3] = {{0}}, R[2][3] = {{0}};
        int m = 0;
        for (int i = 0; i < n; i++) {
            double x = px[i] - 160, y = py[i] - 128, dx = qx[i], dy = qy[i];
            if (it && (fabs(dx - (B[0][0] * x + B[0][1] * y + B[0][2])) > 6 || fabs(dy - (B[1][0] * x + B[1][1] * y + B[1][2])) > 6)) continue;
            double v[3] = {x, y, 1};
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) M[r][c] += v[r] * v[c];
                R[0][r] += v[r] * dx;
                R[1][r] += v[r] * dy;
            }
            m++;
        }
        if (m < minn) break;
        double det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                     M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
        if (fabs(det) < 1e-6) break;
        for (int k = 0; k < 2; k++)
            for (int c = 0; c < 3; c++) {
                double T[3][3];
                memcpy(T, M, sizeof T);
                for (int r = 0; r < 3; r++) T[r][c] = R[k][r];
                B[k][c] = (T[0][0] * (T[1][1] * T[2][2] - T[1][2] * T[2][1]) - T[0][1] * (T[1][0] * T[2][2] - T[1][2] * T[2][0]) +
                           T[0][2] * (T[1][0] * T[2][1] - T[1][1] * T[2][0])) / det;
            }
        ok = 1;
    }
    if (ok) memcpy(A, B, sizeof B);
    return ok;
}
static int16_t fx[MAXV], fy[MAXV], fdx[MAXV], fdy[MAXV];

/* ---- etapa 8, paso 3b: interpolacion rigida por pieza ---- */
int g_rigid = -1;   /* PSX_INTERP_RIGID=1 la activa (prueba; por defecto no: abre costuras entre piezas, ver docs/14-etapa8.md) */
int st_rigid, st_rigid_v, st_rw[6];   /* st_rw: tramos, sin mayoria, otra proyeccion, giro, salto, no cuadra */
static void unpack_r(const int32_t *c, double r[3][3]) {
    r[0][0] = (int16_t)c[0]; r[0][1] = (int16_t)(c[0] >> 16); r[0][2] = (int16_t)c[1];
    r[1][0] = (int16_t)(c[1] >> 16); r[1][1] = (int16_t)c[2]; r[1][2] = (int16_t)(c[2] >> 16);
    r[2][0] = (int16_t)c[3]; r[2][1] = (int16_t)(c[3] >> 16); r[2][2] = (int16_t)c[4];
}
/* la matriz a medio camino: media, filas otra vez ortogonales (con su largo medio: puede haber
 * escala). 0 si las dos rotaciones estan demasiado lejos para mezclarlas */
static int mid_matrix(const Mat *a, const Mat *b, int32_t out[8]) {
    double ra[3][3], rb[3][3], r[3][3], len[3];
    unpack_r(a->c, ra); unpack_r(b->c, rb);
    for (int i = 0; i < 3; i++) {
        double la = 0, lb = 0, dot = 0;
        for (int j = 0; j < 3; j++) { la += ra[i][j] * ra[i][j]; lb += rb[i][j] * rb[i][j]; dot += ra[i][j] * rb[i][j]; }
        la = sqrt(la); lb = sqrt(lb);
        if (la < 1 || lb < 1 || dot < 0.5 * la * lb) return 0;   /* mas de 60 grados */
        len[i] = (la + lb) / 2;
        for (int j = 0; j < 3; j++) r[i][j] = ra[i][j] / la + rb[i][j] / lb;
    }
    for (int i = 0; i < 3; i++) {   /* Gram-Schmidt */
        for (int k = 0; k < i; k++) {
            double d = 0;
            for (int j = 0; j < 3; j++) d += r[i][j] * r[k][j];
            for (int j = 0; j < 3; j++) r[i][j] -= d * r[k][j];
        }
        double l = sqrt(r[i][0] * r[i][0] + r[i][1] * r[i][1] + r[i][2] * r[i][2]);
        if (l < 1e-9) return 0;
        for (int j = 0; j < 3; j++) r[i][j] /= l;
    }
    int16_t q[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            double v = r[i][j] * len[i];
            q[i * 3 + j] = (int16_t)lrint(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    out[0] = (uint16_t)q[0] | (int32_t)((uint32_t)(uint16_t)q[1] << 16);
    out[1] = (uint16_t)q[2] | (int32_t)((uint32_t)(uint16_t)q[3] << 16);
    out[2] = (uint16_t)q[4] | (int32_t)((uint32_t)(uint16_t)q[5] << 16);
    out[3] = (uint16_t)q[6] | (int32_t)((uint32_t)(uint16_t)q[7] << 16);
    out[4] = q[8];
    for (int i = 5; i < 8; i++) out[i] = (int32_t)(((int64_t)a->c[i] + b->c[i]) / 2);
    return 1;
}
static void rigid_pieces(void) {
    st_rigid = st_rigid_v = 0;
    memset(st_rw, 0, sizeof st_rw);
    /* pieza anterior de cada pieza: la que mas vertices emparejados le da */
    static int from[MAXM], votes[MAXM], second[MAXM];
    for (int m = 0; m < nmat; m++) from[m] = -1, votes[m] = second[m] = 0;
    /* los vertices de una pieza son seguidos: se recorre por tramos */
    for (int a = 0; a < ncur;) {
        int b = a, m = cur[a].mat;
        while (b < ncur && cur[b].mat == m) b++;
        /* mayoria entre las piezas anteriores de sus parejas */
        enum { NC = 8 };
        int cand[NC], cnt[NC], nc = 0;
        for (int k = a; k < b; k++) {
            if (!pok[k] || !in3d[k]) continue;
            int pm = prv[pbest[k]].mat, j = 0;
            while (j < nc && cand[j] != pm) j++;
            if (j == nc) { if (nc == NC) continue; cand[nc] = pm; cnt[nc] = 0; nc++; }
            cnt[j]++;
        }
        int best = -1, bc = 0, sc = 0;
        for (int j = 0; j < nc; j++) {
            if (cnt[j] > bc) { sc = bc; bc = cnt[j]; best = cand[j]; }
            else if (cnt[j] > sc) sc = cnt[j];
        }
        st_rw[0]++;
        if (!(best >= 0 && bc >= 3 && bc >= 3 * sc && best < npmat)) st_rw[1]++;
        else if (!(pmats[best].ofx == mats[m].ofx && pmats[best].ofy == mats[m].ofy && pmats[best].h == mats[m].h)) st_rw[2]++;
        if (best >= 0 && bc >= 3 && bc >= 3 * sc && best < npmat) {
            int32_t mc[8];
            Mat *A = &pmats[best], *B = &mats[m];
            int same = A->ofx == B->ofx && A->ofy == B->ofy && A->h == B->h, mm = same && mid_matrix(A, B, mc);
            if (same && !mm) st_rw[3]++;
            if (mm) {
                /* proyeccion de cada vertice con la matriz a medio camino (la GTE del juego, con su
                 * pantalla ancha), sin pasar por el enganche de la interpolacion */
                static PsxGte gt;
                void (*hook)(int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t) = psx_gte_vertex_hook;
                psx_gte_vertex_hook = NULL;
                memcpy(&gt, psx_gte_get_context(), sizeof gt);
                memcpy(gt.c, mc, sizeof mc);
                gt.c[24] = B->ofx; gt.c[25] = B->ofy; gt.c[26] = B->h;
                if (getenv("PSX_INTERP_RIGID_CHK")) {   /* depuracion: con la matriz del propio paso debe salir lo registrado */
                    static int shown;
                    PsxGte g2 = gt;
                    memcpy(g2.c, B->c, sizeof B->c);
                    g2.c[24] = B->ofx; g2.c[25] = B->ofy; g2.c[26] = B->h;
                    for (int k = a; k < b && shown < 6; k++) {
                        g2.d[0] = (int32_t)((uint32_t)(uint16_t)cur[k].vx | (uint32_t)(uint16_t)cur[k].vy << 16);
                        g2.d[1] = cur[k].vz;
                        psx_gte_command(&g2, 0x0180001);
                        if ((uint32_t)g2.d[14] != cur[k].sxy) {
                            fprintf(stderr, "rigida: vertice %d (%d,%d,%d) registrado (%d,%d), rehecho (%d,%d); ofx %08x ofy %08x h %d, contexto %08x %08x %d\n", k, cur[k].vx, cur[k].vy, cur[k].vz,
                                    lo(cur[k].sxy), hi(cur[k].sxy), (int16_t)(g2.d[14] & 0xFFFF), (int16_t)(g2.d[14] >> 16), B->ofx, B->ofy, B->h,
                                    ((PsxGte *)psx_gte_get_context())->c[24], ((PsxGte *)psx_gte_get_context())->c[25], ((PsxGte *)psx_gte_get_context())->c[26]);
                            shown++;
                        }
                    }
                }
                int16_t nx[4096], ny[4096];
                int nv = b - a > 4096 ? 0 : b - a, okk = 1;
                double dev = 0;
                int ndev = 0;
                for (int k = 0; k < nv; k++) {
                    gt.d[0] = (int32_t)((uint32_t)(uint16_t)cur[a + k].vx | (uint32_t)(uint16_t)cur[a + k].vy << 16);
                    gt.d[1] = cur[a + k].vz;
                    psx_gte_command(&gt, 0x0180001);   /* RTPS */
                    uint32_t sxy = (uint32_t)gt.d[14];
                    nx[k] = (int16_t)(sxy & 0xFFFF); ny[k] = (int16_t)(sxy >> 16);
                    int x1 = lo(cur[a + k].sxy), y1 = hi(cur[a + k].sxy);
                    /* desplazamiento hacia el paso anterior que da esa posicion en t = 1/2 */
                    int dx = 2 * (nx[k] - x1), dy = 2 * (ny[k] - y1);
                    if (abs(dx) > JUMP_FIT || abs(dy) > JUMP_FIT) okk = 0;
                    if (pok[a + k] && in3d[a + k]) { dev += hypot(dx - pdx[a + k], dy - pdy[a + k]); ndev++; }
                }
                psx_gte_vertex_hook = hook;
                /* las parejas tienen que cuadrar con la pieza (si no, el emparejamiento era de otra) */
                if (!okk) st_rw[4]++;
                else if (!(ndev && dev / ndev < 6 + 0.1 * nv)) st_rw[5]++;
                if (nv && okk && ndev && dev / ndev < 6 + 0.1 * nv) {
                    for (int k = 0; k < nv; k++) {
                        if (!in3d[a + k]) continue;
                        pdx[a + k] = (int16_t)(2 * (nx[k] - lo(cur[a + k].sxy)));
                        pdy[a + k] = (int16_t)(2 * (ny[k] - hi(cur[a + k].sxy)));
                        pok[a + k] = 1;
                        st_rigid_v++;
                    }
                    st_rigid++;
                }
            }
        }
        a = b;
    }
}

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

static int g_parity;
/* prueba: el paso que va a dibujar psx_interp_draw sirve de referencia (lo usa el redibujo para
 * interpolar entre los pasos k y k+S+1) */
int psx_interp_next_is_ref(void) { return !psx_interp_skip_test || g_parity % (psx_interp_skip_test + 1) == 0; }

int psx_interp_draw(void *gpu, uint32_t ot) {
    PsxGpu *g = gpu;
    int ph = psx_interp_skip_test ? g_parity++ % (psx_interp_skip_test + 1) : 0;
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
    walk(ot, (g->off_x & 0x7FF) | (g->off_y & 0x7FF) << 11, (g->area_x1 & 0x3FF) | (g->area_y1 & 0x3FF) << 10);
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
    /* 0. el espacio 3D: el espacio de dibujo (desplazamiento E5 + area E3) con mas coordenadas
     *    de la GTE (en la mision, 160,384 a pantalla completa). Solo eso se interpola. El HUD, el radar y el mapa tambien usan la GTE
     *    (giran) pero en sus propias ventanas y desplazamientos: interpolarlos como parte de la
     *    escena desplazaba bloques del mapa y cortaba cunas del radar, y sus vertices falseaban
     *    el movimiento global de la camara (Retroid 1.4) */
    for (int i = 0; i < nref; i++) {   /* vertice de la GTE de cada coordenada de la lista */
        Slot *sv = slot(valt, val_gen, mix(*ref[i] | (1ull << 40)), 0);
        refk[i] = sv && sv->val ? (int)sv->val - 1 : -1;
    }
    int64_t spc3d = -1;
    {
        int64_t ov[16];
        int oc[16], no = 0, best = 0;   /* recuento por espacio (hay pocos) */
        for (int i = 0; i < nref; i++) {
            if (refk[i] < 0) continue;
            int j = 0;
            while (j < no && ov[j] != ref_spc[i]) j++;
            if (j == no) { if (no == 16) continue; ov[no] = ref_spc[i]; oc[no++] = 0; }
            if (++oc[j] > best) { best = oc[j]; spc3d = ov[j]; }
        }
    }
    int ofs3d = (int)(spc3d & 0x3FFFFF);
    for (int i = 0; i < ncur; i++) in3d[i] = 0;
    for (int i = 0; i < nref; i++) {
        if (refk[i] >= 0 && ref_spc[i] != spc3d) refk[i] = -1;   /* fuera del espacio 3D: quieto */
        else if (refk[i] >= 0) in3d[refk[i]] = 1;
    }
    /* apariciones de cada clave del modelo contando solo los vertices del espacio 3D: el juego
     * vuelve a transformar los mismos vertices para otras cosas (el radar, en pasos alternos) y
     * entonces ninguna clave salia unica: el movimiento global fallaba uno de cada dos pasos y
     * ~12 % del terreno se quedaba quieto en las intermedias (parpadeo a 12,5 Hz) */
    occ3_gen++;
    for (int i = 0; i < ncur; i++)
        if (in3d[i]) { Slot *o = slot(occ3, occ3_gen, cur[i].base, 1); if (o) o->cnt++; }
    /* 1. pares sin ambiguedad (clave unica en los dos pasos) -> movimiento global afin:
     *    d = P0 - P1 = A * (P1 - centro) + e, por minimos cuadrados con un rechazo */
    double A[2][3] = {{0, 0, 0}, {0, 0, 0}};
    int model = 0;
    if (getenv("PSX_INTERP_DBG2")) {
        int a = 0, b = 0, c = 0, d = 0;
        for (int i = 0; i < ncur; i++) {
            if (!in3d[i]) continue;
            a++;
            Slot *o = slot(occ3, occ3_gen, cur[i].base, 0), *p = slot(prevt, prev_gen, cur[i].base, 0);
            if (o && o->cnt == 1) b++;
            if (p) c++;
            if (o && o->cnt == 1 && p && p->cnt == 1) d++;
        }
        fprintf(stderr, "ajuste: 3d %d unicos %d en el anterior %d unicos en los dos %d\n", a, b, c, d);
    }
    if (have_prev) {
        int n = 0;
        for (int i = 0; i < ncur; i++) {
            Slot *o = slot(occ3, occ3_gen, cur[i].base, 0), *p = slot(prevt, prev_gen, cur[i].base, 0);
            if (!in3d[i] || !o || o->cnt != 1 || !p || p->cnt != 1) continue;
            int dx = lo(prv[p->val - 1].sxy) - lo(cur[i].sxy), dy = hi(prv[p->val - 1].sxy) - hi(cur[i].sxy);
            if (abs(dx) > JUMP_FIT || abs(dy) > JUMP_FIT) continue;
            fx[n] = lo(cur[i].sxy); fy[n] = hi(cur[i].sxy); fdx[n] = (int16_t)dx; fdy[n] = (int16_t)dy; n++;
        }
        model = fit_affine(fx, fy, fdx, fdy, n, 12, A);
    }
    /* 2. pareja de cada vertice. Primero las claves unicas en los dos pasos (con la prediccion
     *    del movimiento global solo para admitir saltos grandes). Luego las repetidas (casillas
     *    del terreno con coordenadas locales): el candidato mas cercano a la prediccion de sus
     *    vecinos, la media de los vertices unicos de sus mismos poligonos (al lado en 3D); el
     *    movimiento global solo si no tiene vecinos emparejados. Con solo el global, en giros
     *    fuertes se elegia el candidato equivocado, la malla se revolvia en la silueta y
     *    asomaban caras de detras (puntos oscuros en el terreno lejano, Retroid 1.3) */
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
            if (!have_prev || !in3d[i]) continue;
            Slot *p = slot(prevt, prev_gen, cur[i].base, 0);
            if (!p) continue;
            Slot *o = slot(occ3, occ3_gen, cur[i].base, 0);
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
            pdx[i] = (int16_t)dx; pdy[i] = (int16_t)dy; pok[i] = 1; pbest[i] = best;
        }
    }
    /* 2b. el movimiento global otra vez, con todas las parejas 3D (las repetidas ya resueltas por
     *     sus vecinos): con solo las claves unicas a veces no hay bastantes (el terreno repite
     *     coordenadas) y entonces lo que queda sin pareja no se podia mover */
    {
        int n = 0;
        for (int i = 0; i < ncur; i++)
            if (pok[i] && in3d[i]) { fx[n] = lo(cur[i].sxy); fy[n] = hi(cur[i].sxy); fdx[n] = pdx[i]; fdy[n] = pdy[i]; n++; }
        if (fit_affine(fx, fy, fdx, fdy, n, 24, A)) model = 1;
    }
    /* 2c. parejas que no cuadran con sus vecinos de malla (paso 19). Una clave del modelo unica en
     *     los dos pasos puede ser de dos piezas distintas e iguales (tramos de puente, edificios
     *     repetidos de las ciudades: el A visible en un paso, el B en el siguiente); el vertice
     *     viajaria en las intermedias de una pieza a otra y el poligono se estiraria 1-3 imagenes.
     *     En un paso, los vertices de un mismo objeto se mueven casi igual: se descarta la pareja
     *     que se aparta de la mediana de sus vecinos (los otros vertices de sus poligonos) mas de
     *     lo que explican el giro de la camara y el tamano del poligono; el vertice se mueve
     *     entonces con sus vecinos (paso 3) */
    st_reject = 0;
    /* 2c-1. por objeto (la matriz con que lo proyecto la GTE): la mayoria de sus vertices viene de
     *       un objeto del paso anterior; los que vienen de otro (una pieza igual de al lado, aunque
     *       sean varios y se den la razon entre si) se descartan */
    if (have_prev) {
        static Slot vote[HSIZE], best[HSIZE];
        static uint32_t vote_gen, best_gen;
        vote_gen++; best_gen++;
        for (int k = 0; k < ncur; k++) {
            if (!pok[k] || !in3d[k]) continue;
            Slot *v = slot(vote, vote_gen, mix(((uint64_t)cur[k].grp << 32) | prv[pbest[k]].grp), 1);
            if (v) v->cnt++;
        }
        for (int k = 0; k < ncur; k++) {
            if (!pok[k] || !in3d[k]) continue;
            Slot *v = slot(vote, vote_gen, mix(((uint64_t)cur[k].grp << 32) | prv[pbest[k]].grp), 0);
            Slot *b = slot(best, best_gen, mix((uint64_t)cur[k].grp | (3ull << 40)), 1);
            if (v && b && v->cnt > b->cnt) { b->cnt = v->cnt; b->val = prv[pbest[k]].grp; }
        }
        for (int k = 0; k < ncur; k++) {
            if (!pok[k] || !in3d[k]) continue;
            Slot *b = slot(best, best_gen, mix((uint64_t)cur[k].grp | (3ull << 40)), 0);
            if (b && b->cnt >= 3 && prv[pbest[k]].grp != b->val) { pok[k] = 0; st_reject++; }
        }
    }
    if (have_prev) {
        /* por vecino: tolerancia segun el lado que los une (un lado largo admite mas diferencia por
         * perspectiva) y el movimiento del vecino (giros rapidos); se descarta si no cuadra con la
         * mayoria */
        enum { NB = 12 };
        static int16_t nbx[MAXV][NB], nby[MAXV][NB];
        static uint16_t nbl[MAXV][NB];
        static uint8_t nbn[MAXV], rej[MAXV];
        for (int i = 0; i < ncur; i++) { nbn[i] = 0; rej[i] = 0; }
        for (int a = 0; a < nref;) {
            int b = a;
            while (b < nref && ref_prim[b] == ref_prim[a]) b++;
            if (b - a <= 4)
                for (int i = a; i < b; i++) {
                    int ki = refk[i];
                    if (ki < 0) continue;
                    for (int j = a; j < b; j++) {
                        int kj = refk[j];
                        if (j == i || kj < 0 || kj == ki || !pok[kj] || nbn[ki] >= NB) continue;
                        int ex = abs(lo(cur[ki].sxy) - lo(cur[kj].sxy)), ey = abs(hi(cur[ki].sxy) - hi(cur[kj].sxy));
                        nbx[ki][nbn[ki]] = pdx[kj]; nby[ki][nbn[ki]] = pdy[kj];
                        nbl[ki][nbn[ki]] = (uint16_t)(ex > ey ? ex : ey);
                        nbn[ki]++;
                    }
                }
            a = b;
        }
        for (int k = 0; k < ncur; k++) {
            if (!pok[k] || !in3d[k] || nbn[k] < 2) continue;
            int bad = 0;
            for (int j = 0; j < nbn[k]; j++) {
                double dev = hypot(pdx[k] - nbx[k][j], pdy[k] - nby[k][j]);
                double tol = 6 + 0.25 * hypot(nbx[k][j], nby[k][j]) + 0.3 * nbl[k][j];
                bad += dev > tol;
            }
            if (bad * 2 > nbn[k]) rej[k] = 1;
        }
        for (int k = 0; k < ncur; k++)
            if (rej[k]) { pok[k] = 0; st_reject++; }
    }
    /* 2d. (etapa 8, paso 3b) piezas enteras: la pieza (matriz de la GTE) cuya mayoria de vertices
     *     emparejados viene de una misma pieza del paso anterior se interpola entera: rotacion y
     *     traslacion a medio camino, y todos sus vertices (tambien los que no tenian pareja) se
     *     vuelven a proyectar con la GTE. Sin estiramientos ni piezas partidas */
    if (g_rigid < 0) g_rigid = getenv("PSX_INTERP_RIGID") ? atoi(getenv("PSX_INTERP_RIGID")) : 0;   /* medido: mas estirados (costuras) */
    if (have_prev && g_rigid) rigid_pieces();
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
    int sky_exact = 0, sky_model = 0, sky_none = 0;   /* medicion: como se mueve el fondo (PSX_INTERP_SKY) */
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
        else if (ref_spc[a] == spc3d) {
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
                if (exact) sky_exact++;
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
            int big = b - a >= 3 && (x1 - x0 >= BACKDROP || y1 - y0 >= BACKDROP);
            if (big && !exact) { if (model) sky_model++; else sky_none++; }
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
    if (getenv("PSX_INTERP_SKY") && (sky_exact || sky_model || sky_none))
        fprintf(stderr, "cielo: VBlank %d, exactas %d, por el modelo %d, quietas %d\n", VSync(-1), sky_exact, sky_model, sky_none);
    for (int i = 0; i < nref; i++) {
        if (moved[i] != 3) continue;
        Slot *d = slot(dragt, drag_gen, mix(orig[i] | (2ull << 40)), 0);
        if (!d || !d->cnt) { ddx[i] = ddy[i] = 0; continue; }
        int n = (int)d->cnt, x = dsx[d - dragt], y = dsy[d - dragt];
        ddx[i] = (int16_t)((x >= 0 ? x + n / 2 : x - n / 2) / n);
        ddy[i] = (int16_t)((y >= 0 ? y + n / 2 : y - n / 2) / n);
    }
    /* medicion (paso 19): poligonos 3D cuya forma "de antes" (con las parejas) no cuadra con la
     * actual: un objeto rigido apenas cambia en un paso, asi que un lado que cambia mucho delata un
     * emparejamiento falso (el vertice viaja de una pieza a otra en las intermedias: se estira) */
    {
        static const int Q[4] = {0, 1, 3, 2};
        int nst = 0, n3d = 0;
        for (int a = 0; a < nref;) {
            int b = a;
            while (b < nref && ref_prim[b] == ref_prim[a]) b++;
            int nv = b - a, ok = nv >= 3 && nv <= 4 && ref_prim[a] < MAXREF && prim_poly[ref_prim[a]];
            for (int i = a; ok && i < b; i++) ok = refk[i] >= 0 && moved[i];
            n3d += ok;
            if (ok)
                for (int e = 0; e < nv; e++) {
                    int p = a + (nv == 4 ? Q[e] : e), q = a + (nv == 4 ? Q[(e + 1) & 3] : (e + 1) % nv);
                    double cx = lo(orig[p]) - lo(orig[q]), cy = hi(orig[p]) - hi(orig[q]);
                    double px = cx + ddx[p] - ddx[q], py = cy + ddy[p] - ddy[q];
                    double lc = sqrt(cx * cx + cy * cy), lp = sqrt(px * px + py * py);
                    if (fabs(lp - lc) > 12 && (lp > 1.5 * lc || lc > 1.5 * lp)) {
                        nst++;
                        if (getenv("PSX_INTERP_STRETCH") && nst <= 4) {   /* donde: vertices actuales y desplazamientos */
                            fprintf(stderr, "  estirado VBlank %d:", VSync(-1));
                            for (int i = a; i < b; i++)
                                fprintf(stderr, " (%d,%d)%+d%+d%s", lo(orig[i]), hi(orig[i]), ddx[i], ddy[i], moved[i] == 2 ? "" : "*");
                            fprintf(stderr, "\n");
                        }
                        /* que no se estire: en las intermedias se mueve entero con la mediana de sus
                         * vertices (sus coordenadas son suyas: no tocan las de otros poligonos) */
                        int16_t mx[4], my[4];
                        for (int i = 0; i < nv; i++) { mx[i] = ddx[a + i]; my[i] = ddy[a + i]; }
                        for (int i = 1; i < nv; i++)
                            for (int j = i; j > 0 && mx[j] < mx[j - 1]; j--) { int16_t t = mx[j]; mx[j] = mx[j - 1]; mx[j - 1] = t; }
                        for (int i = 1; i < nv; i++)
                            for (int j = i; j > 0 && my[j] < my[j - 1]; j--) { int16_t t = my[j]; my[j] = my[j - 1]; my[j - 1] = t; }
                        int16_t cx2 = (int16_t)((mx[(nv - 1) / 2] + mx[nv / 2]) / 2), cy2 = (int16_t)((my[(nv - 1) / 2] + my[nv / 2]) / 2);
                        for (int i = a; i < b; i++) { ddx[i] = cx2; ddy[i] = cy2; }
                        break;
                    }
                }
            a = b;
        }
        st_stretch += nst;
        /* muchisimos a la vez: no son parejas sueltas sino un corte de camara (otro plano): ese paso
         * va sin intermedias (si no, se mezclarian dos vistas sin relacion) */
        g_cut = nst > 40 && nst * 10 > n3d;
        st_cuts += g_cut;
        st_rejects += st_reject;
        st_stretch_steps++;
        static int reg;
        if (!reg && getenv("PSX_INTERP_STRETCH")) { reg = 1; atexit(stretch_summary); }
        if (nst && getenv("PSX_INTERP_STRETCH")) fprintf(stderr, "estirados: VBlank %d, %d poligonos (parejas descartadas %d)\n", VSync(-1), nst, st_reject);
    }
    int dump_now = getenv("PSX_INTERP_PRIMDUMP") && (getenv("PSX_INTERP_PROBE") ? abs(VSync(-1) - atoi(getenv("PSX_INTERP_PROBE"))) <= 1 : tm_n == 50);
    if (dump_now) {   /* depuracion: primitiva, coordenada, estado */
        FILE *f = fopen(getenv("PSX_INTERP_PRIMDUMP"), "w");
        if (f) {
            for (int i = 0; i < nref; i++) fprintf(f, "%d %d %d %d %d %d %d %d %llx %llx\n", ref_prim[i], lo(orig[i]), hi(orig[i]), moved[i], ddx[i], ddy[i],
                                              ((ref_ofs[i] & 0x7FF) ^ 0x400) - 0x400, (((ref_ofs[i] >> 11) & 0x7FF) ^ 0x400) - 0x400,
                                              (unsigned long long)ref_spc[i], (unsigned long long)spc3d);
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
    memcpy(pmats, mats, (size_t)nmat * sizeof mats[0]);
    npmat = nmat;
    nmat = 0;
    nprv = ncur;
    for (int i = ncur - 1; i >= 0; i--) {
        prv[i] = cur[i];
        prv[i].next = -1;
        if (!in3d[i]) continue;   /* solo el espacio 3D sirve de referencia */
        Slot *s = slot(prevt, prev_gen, cur[i].base, 1);
        if (!s) { prv[i].next = -1; continue; }
        prv[i].next = (int)s->val - 1;
        s->val = (uint32_t)i + 1;
        s->cnt++;
    }
    /* coordenadas 3D que no se pueden mover (sin pareja, sin vecinos emparejados ni movimiento
     * global): si son muchas, la intermedia mezclaria terreno movido y quieto y se abririan
     * grietas por las que se ve el cielo (giros de camara bruscos con la palanca derecha: lo
     * unico emparejado es el avion propio). Ese paso va sin intermedias */
    int stuck = 0;
    for (int i = 0; i < nref; i++) stuck += moved[i] == 1;
    st_stuck = stuck;
    if (getenv("PSX_INTERP_DBG2")) fprintf(stderr, "paso vb %d: gte %d reconocidas %d emparejadas %d arrastradas %d quietas %d modelo %d; piezas enteras %d (%d vertices)\n",
                                           VSync(-1), ncur, st_matched, st_paired, st_dragged, stuck, model, st_rigid, st_rigid_v);
    if (getenv("PSX_INTERP_DBG2")) fprintf(stderr, "  piezas: tramos %d, sin mayoria %d, otra proyeccion %d, giro %d, salto %d, no cuadran %d; matrices %d\n", st_rw[0], st_rw[1], st_rw[2], st_rw[3], st_rw[4], st_rw[5], nmat);
    int ok = !g_cut && have_prev && interval >= g_min_vb && st_matched > 64 && st_paired * 4 >= st_matched && stuck * 33 <= st_matched;
    /* etapa 8: con el redibujo, el mundo sale exacto aunque el emparejamiento falle; lo movil, quieto */
    int nodyn = 0;
    if (!ok && (g_rebuild || g_rra) && have_prev && interval >= g_min_vb && J.nthreads > 0) { ok = 1; nodyn = 1; }
    /* (etapa 8, paso 10) la lista lee como textura la imagen (esta o la del paso anterior): la estela del
     * virus de la mision 19, que mezcla cada imagen con la anterior. Una intermedia no tiene esa imagen
     * anterior (texturas pegadas, cielo temblando en la Retroid): ese paso, solo la real, como en la PS1.
     * PSX_INTERP_NOFB=1: sin esto (para ver el fallo) */
    {
        static int nofb = -1;
        static int prev[4] = {-1, -1, -1, -1};
        if (nofb < 0) nofb = getenv("PSX_INTERP_NOFB") != NULL;
        int rects[2][4] = {{g->area_x1, g->area_y1, g->area_x2, g->area_y2}}, nr = 1;
        if (prev[0] >= 0 && memcmp(prev, rects[0], sizeof prev)) { memcpy(rects[1], prev, sizeof prev); nr = 2; }
        memcpy(prev, rects[0], sizeof prev);
        int tp = g->tp_x | g->tp_y << 4 | g->tp_depth << 7;
        if (!nofb && ok && psx_gpu_list_textures_from(ot, psx_ram, psx_scratch, tp, (const int (*)[4])rects, nr)) {
            ok = 0;
            psx_interp_feedback++;
        }
    }
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
        draw_real(g, ot, 1, 1, interval);
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
            if (nodyn) J.L.n = J.L.np = 0;
            J.rebuilt = g_rebuild ? rebuild_phases(ot, g_nfrac + 1) : 0;
            J.rr_async = g_rra && !g_rra->prep(ot);
            /* prueba (AC3_RR_DYN=0): con el redibujo, lo movil no se interpola (se queda como en el paso) */
            static int dyn = -1;
            if (dyn < 0) dyn = getenv("AC3_RR_DYN") ? atoi(getenv("AC3_RR_DYN")) : 1;
            if (J.rr_async && dyn == 0) J.L.n = J.L.np = 0;
            if (g_rra && !J.rr_async) psx_interp_rebuild_fails++;
            if (nodyn && !J.rebuilt) J.L.n = J.L.np = 0;
            J.ot = ot; J.den = g_nfrac + 1; J.interval = interval; J.t = ta; J.sink = g_sink;
            J.x0 = g->area_x1; J.y0 = g->area_y1;
            J.ax1 = g->area_x1; J.ay1 = g->area_y1; J.ax2 = g->area_x2; J.ay2 = g->area_y2;
            psx_display_geom(&J.dw, &J.dh, &J.on, &J.rgb24);
            if (J.dw * J.dh > 1024 * 512) J.dw = J.dh = 0;
            J.hwsink = J.on && !J.rgb24 ? g_hwsink : NULL;
            J.real_only = g_real_only;
            J.nread = g_real_nread; memcpy(J.read, g_real_read, sizeof J.read);
            if (J.rr_async && !(J.dw > 0 && J.dh > 0)) { g_rra->done(); J.rr_async = 0; }   /* sin trabajo: se suelta */
        }
        double tc = tnow();
        draw_real(g, ot, g_nfrac + 1, g_nfrac + 1, interval);
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
