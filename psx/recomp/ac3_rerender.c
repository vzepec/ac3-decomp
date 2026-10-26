/* Imágenes intermedias redibujando el mundo estático (ver ac3_rerender.h, docs/14-etapa8.md). */
#include "ac3_rerender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gte.h"
#include "psx_libgte.h"
#include "psx_mem.h"
#include "recomp_rt.h"

extern uint8_t *mipsi_rtrace, *mipsi_wtrace;

#include <time.h>
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
double ac3_rr_t[6];   /* medicion: S0, cadena, restaurar, dibujo del juego, cajones; pasos */
static uint32_t rd32(uint32_t a) { return *(uint32_t *)psx_ptr(a); }
static void wr32(uint32_t a, uint32_t v) { *(uint32_t *)psx_ptr(a) = v; }

/* direcciones por versión */
typedef struct {
    uint32_t g_render;                    /* puntero a la estructura de render */
    uint32_t cam_matrix;                  /* FUN_8003304c(cámara, g_render+0x2C, g_render+0x4C) */
    uint32_t draw[4];                     /* dibujo estático, en orden */
    uint32_t after;                       /* la primera función después (fin del dibujo estático) */
    uint32_t view[2];                     /* lógica de los subpasos sin juego (repetición, demostración) */
} RrAddr;
/* view: la tabla de subpasos de la misión (US 0x8009F230 en ACE.BIN, +0x9f230) llama al dibujo de
 * la misión con una lógica por subpaso: 0x80032e6c en vuelo, 0x80032ffc en la demostración del
 * título y la repetición (reproduce un vuelo grabado), 0x80032fac (otra vista sin lógica). EU, por
 * la misma forma: 0x80032de4 y 0x80032e34 */
static const RrAddr RR_EU = {0x800BACC4u, 0x8003304Cu, {0x8003308Cu, 0x80033140u, 0x80033194u, 0x800331D4u}, 0x80039D84u,
                             {0x80032DE4u, 0x80032E34u}};
/* US: por firma desde la EU; FUN_80033140 (constantes distintas) por la secuencia de llamadas de
 * FUN_80033910, el dibujo de la mision */
static const RrAddr RR_US = {0x800BB81Cu, 0x80033214u, {0x80033254u, 0x80033308u, 0x8003335Cu, 0x8003339Cu}, 0x80039F8Cu,
                             {0x80032FACu, 0x80032FFCu}};

/* entradas del dibujo estático que cambian entre pasos (encuesta de la etapa 8: AC3_RR_SURVEY, las
 * 41 misiones US): la cámara y las posiciones del arreglo de objetos móviles del mundo (vehículos,
 * barcos: g_render+0x474 + k * 0x5C, 32 entradas; hasta la 26 usada en la misión 24). Lo demás que
 * cambia (búfer actual, contadores, desplazamientos del doble búfer) se queda como en el paso actual */
enum { F_I32 = 0, F_ANG = 1, F_A16 = 2 };   /* F_A16: angulo de un objeto (short; vuelta = 4096) */
typedef struct { int base; int off; int kind; int group; } RrField;   /* base: 0 = g_render, 1 = cámara */
#define NOBJ 32
#define NFIELDS AC3_RR_NFIELDS
static RrField FIELDS[NFIELDS];
static void fields_init(void) {
    static const RrField cam[9] = {
        {1, 0x04, F_I32, 0}, {1, 0x08, F_I32, 0}, {1, 0x0C, F_I32, 0},   /* posición de la cámara */
        {1, 0x14, F_ANG, 0}, {1, 0x16, F_ANG, 0}, {1, 0x18, F_ANG, 0},   /* ángulos (círculo = 16384) */
        {1, 0x9C, F_I32, 1}, {1, 0xA0, F_I32, 1}, {1, 0xA4, F_I32, 1},   /* vector de la cámara */
    };
    memcpy(FIELDS, cam, sizeof cam);
    /* objetos: posicion (+0, +4, +8) y orientacion (shorts en +0x10, +0x12 y +0x14). El de
     * g_render+0x3C4 tiene la misma forma: en la repeticion del final de mision (y la demostracion
     * del titulo) es el avion que sigue la camara, y lo dibuja el dibujo estatico (paso 3) */
    for (int k = 0; k <= NOBJ; k++) {
        int base = k == 0 ? 0x3C4 : 0x474 + (k - 1) * 0x5C, f = 9 + k * 6;
        for (int j = 0; j < 3; j++) FIELDS[f + j] = (RrField){0, base + j * 4, F_I32, 2 + k};
        FIELDS[f + 3] = (RrField){0, base + 0x10, F_A16, 2 + k};
        FIELDS[f + 4] = (RrField){0, base + 0x12, F_A16, 2 + k};
        FIELDS[f + 5] = (RrField){0, base + 0x14, F_A16, 2 + k};
    }
}

static const Ac3Version *g_v;
static const RrAddr *g_a;
static int g_busy;
static uint32_t g_st0, g_st1, g_sp;                          /* primitivas del estático; pila */
/* S0 en dos búferes: el hilo de dibujo puede estar leyendo el de un paso (ac3_rr_snapshot) mientras
 * el juego guarda el del siguiente en el otro */
static uint8_t g_s0buf[2][PSX_RAM_SIZE], g_s0scrbuf[2][0x400], g_wt[PSX_RAM_SIZE / 32];
static const uint8_t *g_s0 = g_s0buf[0], *g_s0_scr = g_s0scrbuf[0];
static PsxGte g_s0_gte;
static int g_s0_idx, g_s0_ok;
static int g_s0_locked = -1;   /* el búfer que está leyendo el hilo de dibujo (atómico) */
static int32_t g_cur[NFIELDS], g_prev[NFIELDS];
/* encuesta (AC3_RR_SURVEY): lo que el dibujo estatico lee antes de escribirlo y cambia entre pasos */
static uint8_t g_rt[PSX_RAM_SIZE / 32], g_s0_prev[PSX_RAM_SIZE];
static int g_survey = -1, g_survey_steps;
typedef struct { uint32_t a; int n; } SvStat;
static SvStat g_sv[2048];
static int g_nsv;
static void survey_print(void) {
    uint32_t gr = rd32(g_a->g_render), cam = rd32(gr + 40);
    printf("rr encuesta: %d pasos; g_render %08x, camara %08x; palabras leidas antes de escribirlas que cambian:\n", g_survey_steps, gr, cam);
    for (int i = 0; i < g_nsv; i++) {
        for (int j = i + 1; j < g_nsv; j++) if (g_sv[j].a < g_sv[i].a) { SvStat t = g_sv[i]; g_sv[i] = g_sv[j]; g_sv[j] = t; }
        uint32_t A = 0x80000000u | g_sv[i].a;
        char where[40] = "";
        if (A >= cam && A < cam + 0x200) snprintf(where, sizeof where, "camara+%03x", A - cam);
        else if (A >= gr && A < gr + 0x1000) snprintf(where, sizeof where, "g_render+%03x", A - gr);
        if (g_sv[i].n * 4 >= g_survey_steps) printf("  %08x %-14s %d de %d pasos\n", A, where, g_sv[i].n, g_survey_steps);
    }
}
static int g_have_cur, g_have_prev, g_complete, g_mark_vb, g_in_static;
/* el objeto de +0x3C4 solo se interpola en los subpasos sin juego: alli es el avion que dibuja el
 * dibujo estatico; en vuelo, el dibujo estatico lo usa de origen de estelas (paso 3: interpolado,
 * la estela del misil se estiraba) */
static int g_step_view, g_view;
static void mark_view(RCpu *c) { (void)c; if (!g_busy) g_step_view = 1; }
int VSync(int);

static uint32_t field_addr(int i) {
    uint32_t gr = rd32(g_a->g_render);
    return FIELDS[i].base ? rd32(gr + 40) + (uint32_t)FIELDS[i].off : gr + (uint32_t)FIELDS[i].off;
}
static int32_t field_get(int i) {
    uint32_t a = field_addr(i);
    return FIELDS[i].kind != F_I32 ? (int32_t) * (int16_t *)psx_ptr(a) : (int32_t)rd32(a);
}
static void field_set(int i, int32_t v) {
    uint32_t a = field_addr(i);
    if (FIELDS[i].kind != F_I32) *(int16_t *)psx_ptr(a) = (int16_t)v;
    else wr32(a, (uint32_t)v);
}

/* inicio del dibujo estático: S0 y el registro de escrituras */
static void mark0(RCpu *c) {
    if (g_busy) return;
    double t0 = now_s();
    g_st0 = rd32(rd32(g_a->g_render) + 20);
    g_sp = c->r[29];
    /* el búfer libre; si lo está leyendo el hilo de dibujo (va con más de un paso de retraso), este
     * paso queda sin redibujo */
    int idx = g_s0_idx ^ 1;
    g_s0_ok = __atomic_load_n(&g_s0_locked, __ATOMIC_ACQUIRE) != idx;
    if (g_s0_ok) {
        memcpy(g_s0buf[idx], psx_ram, PSX_RAM_SIZE);
        memcpy(g_s0scrbuf[idx], psx_scratch, 0x400);
        g_s0_idx = idx;
        g_s0 = g_s0buf[idx];
        g_s0_scr = g_s0scrbuf[idx];
        g_s0_gte = *(PsxGte *)psx_gte_get_context();
    }
    memset(g_wt, 0, sizeof g_wt);
    mipsi_wtrace = g_wt;
    if (g_survey < 0) { g_survey = getenv("AC3_RR_SURVEY") != NULL; if (g_survey) atexit(survey_print); }
    if (g_survey) { memset(g_rt, 0, sizeof g_rt); mipsi_rtrace = g_rt; }
    /* con la prueba de verdad conocida (AC3_INTERP_TEST), el anterior es el ultimo paso de referencia */
    int psx_interp_next_is_ref(void);
    static int32_t ref_fields[NFIELDS];
    static int have_ref;
    if (psx_interp_next_is_ref()) {
        if (have_ref) { memcpy(g_prev, ref_fields, sizeof g_prev); g_have_prev = 1; }
        for (int i = 0; i < NFIELDS; i++) g_cur[i] = field_get(i);
        memcpy(ref_fields, g_cur, sizeof ref_fields);
        have_ref = 1;
    } else for (int i = 0; i < NFIELDS; i++) g_cur[i] = field_get(i);
    { /* depuracion: AC3_RR_DUMPJ=vb1,vb2, objetos del arreglo que se mueven mucho entre pasos */
        int j0 = 0, j1 = -1;
        if (getenv("AC3_RR_DUMPJ") && sscanf(getenv("AC3_RR_DUMPJ"), "%d,%d", &j0, &j1) == 2 && VSync(-1) >= j0 && VSync(-1) <= j1) {
            uint32_t G = rd32(g_a->g_render);
            for (int k = 0; k <= 32; k++) {
                int f = 9 + k * 6;
                int64_t dx = (int64_t)g_cur[f] - g_prev[f], dy = (int64_t)g_cur[f + 1] - g_prev[f + 1], dz = (int64_t)g_cur[f + 2] - g_prev[f + 2];
                if (llabs(dx) + llabs(dy) + llabs(dz) < (1 << 18)) continue;
                uint32_t b = G + (k == 0 ? 0x3C4 : 0x474 + (k - 1) * 0x5C);
                fprintf(stderr, "obj vb %d k %d d %lld,%lld,%lld:", VSync(-1), k, (long long)dx, (long long)dy, (long long)dz);
                for (int o = 0x18; o < 0x5C; o += 4) fprintf(stderr, " %08x", rd32(b + o));
                fprintf(stderr, "\n");
            }
        }
    }
    g_view = g_step_view;
    g_step_view = 0;
    g_have_cur = 1;
    g_complete = 0;
    g_in_static = 1;
    ac3_rr_t[0] += now_s() - t0;
}
/* fin (la función siguiente): rango de primitivas (sin el E5 que el juego pone justo antes) */
static void mark1(RCpu *c) {
    (void)c;
    if (g_busy) return;
    /* sin el inicio en este mismo paso (pausa: el juego dibuja el HUD pero no el mundo), el rango no vale */
    if (!g_in_static) { g_complete = 0; mipsi_wtrace = NULL; return; }
    g_in_static = 0;
    g_st1 = rd32(rd32(g_a->g_render) + 20) - 12;
    mipsi_wtrace = NULL;
    g_complete = g_s0_ok && g_st1 > g_st0;
    g_mark_vb = VSync(-1);
    if (g_survey) {
        mipsi_rtrace = NULL;
        uint32_t gr = rd32(g_a->g_render), ex0 = rd32(gr + 24) & 0x1FFFFF, ex1 = (rd32(gr + 36) & 0x1FFFFF) + 0x1F800;
        if (g_survey_steps++)
            for (uint32_t w = 0; w < PSX_RAM_SIZE; w += 4) {
                if (!(g_rt[w >> 5] >> ((w >> 2) & 7) & 1) || (w >= ex0 && w < ex1)) continue;
                if (*(uint32_t *)(g_s0 + w) == *(uint32_t *)(g_s0_prev + w)) continue;
                int i = 0;
                while (i < g_nsv && g_sv[i].a != w) i++;
                if (i == g_nsv) { if (g_nsv == 2048) continue; g_sv[g_nsv].a = w; g_sv[g_nsv].n = 0; g_nsv++; }
                g_sv[i].n++;
            }
        memcpy(g_s0_prev, g_s0, sizeof g_s0_prev);
    }
}

int ac3_rr_install(const Ac3Version *v) {
    g_v = v;
    fields_init();
    g_a = !strcmp(v->name, "EU") ? &RR_EU : !strcmp(v->name, "US") ? &RR_US : NULL;
    if (!g_a) return -1;
    mipsi_set_hook(g_a->draw[0], mark0);
    mipsi_set_hook(g_a->after, mark1);
    mipsi_set_hook(g_a->view[0], mark_view);
    mipsi_set_hook(g_a->view[1], mark_view);
    return 0;
}

int ac3_rr_ready(void) { return g_a && g_complete && g_have_prev; }

void ac3_rr_static_range(uint32_t *a0, uint32_t *a1) { *a0 = g_st0; *a1 = g_st1; }

/* llamada a una función del juego, como una interrupción, con la pila del dibujo original (algunas
 * funciones de la época leen variables locales sin iniciar) */
static void guest_call(uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2) {
    RCpu cb;
    memset(&cb, 0, sizeof cb);
    cb.r[4] = a0; cb.r[5] = a1; cb.r[6] = a2;
    cb.r[28] = g_v->gp;
    cb.r[29] = g_sp;
    cb.r[31] = 0x8000FFF0u;
    uint32_t ret = rc_ret;
    rc_ret = cb.r[31];
    rc_call(&cb, addr);
    rc_ret = ret;
}

static int32_t lerp_field(int i, float t) {
    int32_t a = g_prev[i], b = g_cur[i];
    if (FIELDS[i].kind == F_ANG) {   /* el camino corto en un círculo de 16384 */
        int32_t d = (b - a) & 0x3FFF;
        if (d >= 0x2000) d -= 0x4000;
        return a + (int32_t)((float)d * t + (d >= 0 ? 0.5f : -0.5f));
    }
    if (FIELDS[i].kind == F_A16) {   /* el camino corto: en 16 bits si es corto; si no, en una vuelta de 4096 */
        int32_t d = (int16_t)(b - a);
        if (d > 2048 || d < -2048) { d = (b - a) & 0xFFF; if (d >= 0x800) d -= 0x1000; }
        return a + (int32_t)((float)d * t + (d >= 0 ? 0.5f : -0.5f));
    }
    return a + (int32_t)((double)((int64_t)b - a) * t + 0.5);
}

static uint32_t g_dbg_pe = 0xFFFFFFFFu;
static void dbg_chain(uint32_t ot, const char *when) {   /* depuracion: rectangulos y lineas de la cadena */
    if (!getenv("AC3_RR_DEBUG") || VSync(-1) != atoi(getenv("AC3_RR_DEBUG"))) return;
    uint32_t b = ot, n = 0;
    fprintf(stderr, "cadena %s:", when);
    while ((b & 0xFFFFFF) != 0xFFFFFF && n++ < 300000) {
        uint32_t A = b | 0x80000000u, tag = rd32(A), op = (tag >> 24) ? rd32(A + 4) >> 24 : 0;
        int st = (A >= g_st0 && A < g_st1) || A >= g_dbg_pe;
        if (!st) fprintf(stderr, " %06x:%02x", A & 0xFFFFFF, op);
        b = tag & 0xFFFFFF;
    }
    fprintf(stderr, "\n");
}

/* se puede redibujar este paso */
long ac3_rr_why[5];   /* medicion: por que no se redibuja (sin datos, de otro paso, corte de giro, corte de posicion) */
static int rr_check(void) {
    if (!ac3_rr_ready()) { ac3_rr_why[0]++; return -1; }
    /* el dibujo estático tiene que ser de este paso: si no corrió (pausa, menú), el rango guardado
     * ya es otra cosa y desengancharlo rompería la imagen */
    if (VSync(-1) - g_mark_vb > 3) { ac3_rr_why[1]++; return -1; }
    /* corte de plano: la cámara salta (más de 1/8 de vuelta o de 2^21 en posición) */
    for (int i = 0; i < 6; i++) {
        int32_t d = g_cur[i] - g_prev[i];
        if (FIELDS[i].kind == F_ANG) { d &= 0x3FFF; if (d >= 0x2000) d -= 0x4000; if (d > 2048 || d < -2048) { ac3_rr_why[2]++; return -1; } }
        else if (d > (1 << 21) || d < -(1 << 21)) { ac3_rr_why[3]++; return -1; }
    }
    return 0;
}

static int build_core(uint32_t ot, float t);
int ac3_rr_build(uint32_t ot, float t) { return rr_check() ? -1 : build_core(ot, t); }

int ac3_rr_snapshot(RrSnap *s) {
    if (rr_check()) return -1;
    s->st0 = g_st0; s->st1 = g_st1; s->sp = g_sp;
    memcpy(s->cur, g_cur, sizeof s->cur);
    memcpy(s->prev, g_prev, sizeof s->prev);
    s->s0 = g_s0; s->s0_scr = g_s0_scr; s->gte = g_s0_gte;
    s->wide = psx_gte_wide;
    s->view = g_view;
    __atomic_store_n(&g_s0_locked, g_s0_idx, __ATOMIC_RELEASE);
    return 0;
}
void ac3_rr_release(void) { __atomic_store_n(&g_s0_locked, -1, __ATOMIC_RELEASE); }

int ac3_rr_build_from(const RrSnap *s, uint32_t ot, float t) {
    if (!g_a) return -1;
    g_st0 = s->st0; g_st1 = s->st1; g_sp = s->sp;
    memcpy(g_cur, s->cur, sizeof g_cur);
    memcpy(g_prev, s->prev, sizeof g_prev);
    g_s0 = s->s0; g_s0_scr = s->s0_scr; g_s0_gte = s->gte;
    psx_gte_wide = s->wide;
    g_view = s->view;
    return build_core(ot, t);
}

static int build_core(uint32_t ot, float t) {
    if (getenv("AC3_RR_DUMPOBJ")) {
        uint32_t G = rd32(g_a->g_render);
        fprintf(stderr, "obj vb %d:", VSync(-1));
        for (int o = 0x3C4; o < 0x3C4 + 0x5C; o += 4) fprintf(stderr, " %08x", rd32(G + o));
        fprintf(stderr, "\n");
    }
    if (getenv("AC3_RR_SPAN")) fprintf(stderr, "rr: VBlank %d, estatico %08x-%08x (%u bytes)\n", VSync(-1), g_st0, g_st1, g_st1 - g_st0);
    uint32_t gr = rd32(g_a->g_render), cam = rd32(gr + 40);
    dbg_chain(ot, "antes");
    double t0 = now_s(), t1, t2, t3;
    ac3_rr_t[5]++;
    /* 1. quitar de la cadena lo estático; anotar cabecera y cuerpo de cada paquete de la cadena */
    static uint8_t inchain[PSX_RAM_SIZE / 32];
    memset(inchain, 0, sizeof inchain);
    /*    y en cada cajón de la tabla de orden de esta imagen, el paquete que precedía a lo estático (el
     *    ancla): lo redibujado irá justo ahí, en el mismo sitio de la cadena que el original */
    uint32_t ob = rd32(gr + 16) & 0x1FFFFF, oe = ob + 0x4000;
    static uint32_t anchor[4096];
    memset(anchor, 0, sizeof anchor);
    int bucket = -1;
    /*    y el final real de lo que la cadena usa en el búfer de primitivas actual (en la pausa hay
     *    código que reserva más allá de g_render[5] sin moverlo: lo nuevo no puede ir ahí) */
    uint32_t pe = rd32(gr + 20), pb0 = rd32(gr + 32), pb1 = rd32(gr + 36);
    uint32_t pbase = (pe >= pb1 && pe < pb1 + 0x1F800) ? pb1 : pb0, maxend = pe;
    uint32_t other = pbase == pb1 ? pb0 : pb1;   /* el otro bufer (el de la imagen anterior) */
    int other_used = other == pbase;
    uint32_t o_lo = 0xFFFFFFFFu, o_hi = 0;   /* lo que la cadena tiene en el otro bufer */
    uint32_t a = ot, guard = 0;
    while ((a & 0xFFFFFF) != 0xFFFFFF && ++guard < 300000) {
        uint32_t ra = a & 0x1FFFFC, tag = rd32(a | 0x80000000u), next = tag & 0xFFFFFF;
        if (ra >= ob && ra < oe) bucket = (int)((ra - ob) >> 2);
        {
            uint32_t A = a | 0x80000000u, e = A + 4 * ((tag >> 24) + 1);
            if (A >= pbase && A < pbase + 0x1F800 && e > maxend) maxend = e;
            /* (la tabla de orden, que puede estar al final del otro bufer, no cuenta) */
            if (A >= other && A < other + 0x1F800 && !(ra >= ob && ra < oe)) { other_used++; if (A < o_lo) o_lo = A; if (e > o_hi) o_hi = e; }
        }
        for (uint32_t w = 0; w <= (tag >> 24) && ra + 4 * w < PSX_RAM_SIZE; w++) {
            uint32_t q = ra + 4 * w;
            inchain[q >> 5] |= (uint8_t)(1u << ((q >> 2) & 7));
        }
        if (bucket >= 0 && next != 0xFFFFFF && (next | 0x80000000u) >= g_st0 && (next | 0x80000000u) < g_st1 && !anchor[bucket])
            anchor[bucket] = a | 0x80000000u;
        while (next != 0xFFFFFF && (next | 0x80000000u) >= g_st0 && (next | 0x80000000u) < g_st1) {
            if (getenv("AC3_RR_DEBUG") && VSync(-1) == atoi(getenv("AC3_RR_DEBUG")))
                fprintf(stderr, "quito %08x len %u orden %02x (cajon %d)\n", next | 0x80000000u, rd32(next | 0x80000000u) >> 24,
                        rd32((next | 0x80000000u) + 4) >> 24, bucket);
            next = rd32(next | 0x80000000u) & 0xFFFFFF;
        }
        wr32(a | 0x80000000u, (tag & 0xFF000000u) | next);
        a = next | 0x80000000u;
    }
    t1 = now_s(); ac3_rr_t[1] += t1 - t0;
    /* 2. volver a S0 en lo que el dibujo estático escribió (no en tablas de orden, primitivas ni
     *    paquetes de la cadena); las primitivas nuevas, detrás de todas las de esta imagen */
    /*    (etapa 8, paso 12) si no caben (lo redibujado ocupa como lo estatico, hasta un 17 % mas: en la mision 19 son hasta
     *    71 KB y tras las de la imagen quedan 57-65), en el otro bufer, si la cadena no tiene nada en el. Antes, sin
     *    sitio no se redibujaba: la mitad de los pasos de la mision 19 con el mundo quieto en las intermedias (temblor) */
    uint32_t prim_end = (maxend + 3) & ~3u, need = (g_st1 - g_st0) + (g_st1 - g_st0) / 4 + 0x1000;
    uint32_t nlo = prim_end, nhi = pbase + 0x1F800;
    static int force_other = -1;   /* prueba: AC3_RR_OTRO=1, siempre en el otro bufer (si la cadena no lo usa) */
    if (force_other < 0) force_other = getenv("AC3_RR_OTRO") != NULL;
    if (nlo + need > nhi || (force_other && !other_used)) {
        if (other_used || getenv("AC3_RR_NOOTRO")) {   /* sin sitio en el búfer (AC3_RR_NOOTRO=1: como antes) */
            if (getenv("AC3_RR_SITIO")) fprintf(stderr, "rr sitio: VBlank %d estatico %u libres %d, otro bufer %s\n", VSync(-1), g_st1 - g_st0,
                                                (int)(nhi - nlo), other_used ? "en uso" : "sin probar");
            if (getenv("AC3_RR_SITIO") && other_used) fprintf(stderr, "rr otro: %d paquetes, %08x-%08x (bufer %08x-%08x)\n", other_used, o_lo, o_hi, other, other + 0x1F800);
            return -1;
        }
        nlo = other; nhi = other + 0x1F800;
        if ((ob | 0x80000000u) > nlo && (ob | 0x80000000u) < nhi) nhi = ob | 0x80000000u;   /* hasta la tabla de orden */
        if (nlo + need > nhi) {
            if (getenv("AC3_RR_SITIO")) fprintf(stderr, "rr sitio: VBlank %d estatico %u, otro bufer %d\n", VSync(-1), g_st1 - g_st0, (int)(nhi - nlo));
            return -1;
        }
    }
    g_dbg_pe = nlo;
    uint32_t ex0 = rd32(gr + 24) & 0x1FFFFF, ex1 = (rd32(gr + 36) & 0x1FFFFF) + 0x1F800;
    /* toda palabra distinta de S0 (no solo las que escribió el dibujo: también la pila, que leen
     *    algunas funciones sin iniciarla y que otras han reescrito desde entonces) */
    for (uint32_t w = 0; w < PSX_RAM_SIZE; w += 4) {
        if (*(uint32_t *)(psx_ram + w) == *(uint32_t *)(g_s0 + w) || (w >= ex0 && w < ex1)) continue;
        if (inchain[w >> 5] >> ((w >> 2) & 7) & 1) continue;
        *(uint32_t *)(psx_ram + w) = *(uint32_t *)(g_s0 + w);
    }
    memcpy(psx_scratch, g_s0_scr, 0x400);
    *(PsxGte *)psx_gte_get_context() = g_s0_gte;
    wr32(gr + 20, nlo);
    /* 3. las entradas, a medio camino; un grupo (un objeto) cuya posición salta (otra ocupación de la
     *    ranura, aparece, desaparece) se queda como en el paso actual */
    /* (paso 13) en vuelo, el de +0x3C4 (el avion que sigue la camara) se mueve lo mismo que la camara: el dibujo
     * estatico lo dibuja con su distancia a ella (FUN_80065f68, modelo segun la distancia). Quieto en el paso actual,
     * con el postquemador quedaba hasta 3/4 de paso por delante: en la primera intermedia, el modelo lejano (pequeno y
     * sin textura) y en las otras, mas pequeno. Interpolado (como sin juego) vuelve la estela del misil doble del
     * paso 3b. AC3_RR_NOSIGUE=1: quieto, como antes */
    static int nosigue = -1;
    if (nosigue < 0) nosigue = getenv("AC3_RR_NOSIGUE") != NULL;
    for (int i = 0; i < NFIELDS;) {
        int j = i, jump = 0;
        while (j < NFIELDS && FIELDS[j].group == FIELDS[i].group) {
            int32_t d = g_cur[j] - g_prev[j];
            if (FIELDS[j].kind == F_I32 && (d > (1 << 20) || d < -(1 << 20))) jump = 1;
            j++;
        }
        if (FIELDS[i].group == 2 && !g_view) jump = 1;   /* el de +0x3C4, solo sin juego */
        for (int k = i; k < j; k++) field_set(k, t >= 1.0f || jump ? g_cur[k] : lerp_field(k, t));
        if (FIELDS[i].group == 2 && !g_view && !nosigue && t < 1.0f)
            for (int k = 0; k < 3; k++)   /* posicion (los 3 primeros de la camara y del grupo) */
                field_set(i + k, g_cur[i + k] + (lerp_field(k, t) - g_cur[k]));
        i = j;
    }
    t2 = now_s(); ac3_rr_t[2] += t2 - t1;
    /* 4. el dibujo estático del juego */
    g_busy = 1;
    static int nodraw = -1;   /* depuracion: AC3_RR_NODRAW=1, se quita lo estatico sin redibujarlo */
    if (nodraw < 0) nodraw = getenv("AC3_RR_NODRAW") != NULL;
    if (!nodraw) {
        guest_call(g_a->cam_matrix, cam, gr + 0x2C, gr + 0x4C);
        for (int i = 0; i < 4; i++) guest_call(g_a->draw[i], 0, 0, 0);
    }
    g_busy = 0;
    if (getenv("AC3_RR_SITIO")) fprintf(stderr, "rr usa: VBlank %d estatico %u redibujo %u libres %d%s\n", VSync(-1), g_st1 - g_st0,
                                        rd32(gr + 20) - nlo, (int)(nhi - nlo), nlo == prim_end ? "" : " (otro bufer)");
    if (rd32(gr + 20) > nhi) return -1;   /* se paso del final (lo de detras, pisado en esta copia: se descarta) */
    t3 = now_s(); ac3_rr_t[3] += t3 - t2;
    /* 5. en cada cajón: lo de después del estático > lo redibujado > lo de antes (el juego inserta a
     *    la cabeza; lo redibujado entró el último) */
    uint32_t b = ot & 0xFFFFFF;
    static uint32_t seg[8192], ord[8192];
#define IS_NEW(x) (((x) | 0x80000000u) >= nlo && ((x) | 0x80000000u) < nhi)   /* lo redibujado */
    while (b != 0xFFFFFF) {
        uint32_t ab = b & 0x1FFFFF;
        if (!(ab >= ob && ab < oe)) { b = rd32(b | 0x80000000u) & 0xFFFFFF; continue; }
        uint32_t nseg = 0, q = rd32(b | 0x80000000u) & 0xFFFFFF;
        while (q != 0xFFFFFF && nseg < 8192) {
            uint32_t aq = q & 0x1FFFFF;
            if (aq >= ob && aq < oe) break;
            seg[nseg++] = q;
            q = rd32(q | 0x80000000u) & 0xFFFFFF;
        }
        /* el orden relativo de lo que ya estaba no se toca; lo nuevo va donde estaba lo estático (tras
         * el ancla) o, si en este cajón no había, al final (polígonos del mundo a una profundidad nueva:
         * que no tapen nada que el juego pusiera aquí) */
        uint32_t tail = q, no = 0;
        uint32_t anc = anchor[(ab - ob) >> 2];
        int placed = 0;
        if (anc && (anc & 0x1FFFFF) == ab) {   /* el ancla es el propio cajón: lo nuevo, a la cabeza */
            for (uint32_t i = 0; i < nseg; i++) if (IS_NEW(seg[i])) ord[no++] = seg[i];
            placed = 1;
        }
        for (uint32_t i = 0; i < nseg; i++) {
            if (IS_NEW(seg[i])) continue;
            ord[no++] = seg[i];
            if (!placed && anc && (seg[i] | 0x80000000u) == anc) {
                for (uint32_t j = 0; j < nseg; j++) if (IS_NEW(seg[j])) ord[no++] = seg[j];
                placed = 1;
            }
        }
        if (!placed)
            for (uint32_t i = 0; i < nseg; i++) if (IS_NEW(seg[i])) ord[no++] = seg[i];
        int changed = 0;
        for (uint32_t i = 0; i < nseg; i++) changed |= ord[i] != seg[i];
        if (getenv("AC3_RR_DEBUG") && VSync(-1) == atoi(getenv("AC3_RR_DEBUG")) && nseg) {
            fprintf(stderr, "cajon %u (ancla %08x): ", (ab - ob) >> 2, anchor[(ab - ob) >> 2]);
            for (uint32_t i = 0; i < nseg && i < 40; i++) {
                uint32_t A = seg[i] | 0x80000000u;
                fprintf(stderr, "%c", IS_NEW(A) ? 'N' : (A >= g_st1) ? 'D' : (A >= g_st0) ? 'S' : 'a');
            }
            fprintf(stderr, " -> ");
            for (uint32_t i = 0; i < no && i < 40; i++) {
                uint32_t A = ord[i] | 0x80000000u;
                fprintf(stderr, "%c", IS_NEW(A) ? 'N' : (A >= g_st1) ? 'D' : (A >= g_st0) ? 'S' : 'a');
            }
            fprintf(stderr, "\n");
        }
        if (changed) {
            uint32_t prevp = b;
            for (uint32_t i = 0; i < nseg; i++) {
                wr32(prevp | 0x80000000u, (rd32(prevp | 0x80000000u) & 0xFF000000u) | ord[i]);
                prevp = ord[i];
            }
            wr32(prevp | 0x80000000u, (rd32(prevp | 0x80000000u) & 0xFF000000u) | tail);
        }
        b = tail;
    }
    ac3_rr_t[4] += now_s() - t3;
    dbg_chain(ot, "despues");
    return 0;
}
