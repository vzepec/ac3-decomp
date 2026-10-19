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

static uint32_t rd32(uint32_t a) { return *(uint32_t *)psx_ptr(a); }
static void wr32(uint32_t a, uint32_t v) { *(uint32_t *)psx_ptr(a) = v; }

/* direcciones por versión */
typedef struct {
    uint32_t g_render;                    /* puntero a la estructura de render */
    uint32_t cam_matrix;                  /* FUN_8003304c(cámara, g_render+0x2C, g_render+0x4C) */
    uint32_t draw[4];                     /* dibujo estático, en orden */
    uint32_t after;                       /* la primera función después (fin del dibujo estático) */
} RrAddr;
static const RrAddr RR_EU = {0x800BACC4u, 0x8003304Cu, {0x8003308Cu, 0x80033140u, 0x80033194u, 0x800331D4u}, 0x80039D84u};
/* US: por firma desde la EU; FUN_80033140 (constantes distintas) por la secuencia de llamadas de
 * FUN_80033910, el dibujo de la mision */
static const RrAddr RR_US = {0x800BB81Cu, 0x80033214u, {0x80033254u, 0x80033308u, 0x8003335Cu, 0x8003339Cu}, 0x80039F8Cu};

/* entradas del dibujo estático que cambian entre pasos (encuesta de la etapa 8: AC3_RR_SURVEY, las
 * 41 misiones US): la cámara y las posiciones del arreglo de objetos móviles del mundo (vehículos,
 * barcos: g_render+0x474 + k * 0x5C, 32 entradas; hasta la 26 usada en la misión 24). Lo demás que
 * cambia (búfer actual, contadores, desplazamientos del doble búfer) se queda como en el paso actual */
enum { F_I32 = 0, F_ANG = 1 };
typedef struct { int base; int off; int kind; int group; } RrField;   /* base: 0 = g_render, 1 = cámara */
#define NOBJ 32
#define NFIELDS (9 + NOBJ * 3)
static RrField FIELDS[NFIELDS];
static void fields_init(void) {
    static const RrField cam[9] = {
        {1, 0x04, F_I32, 0}, {1, 0x08, F_I32, 0}, {1, 0x0C, F_I32, 0},   /* posición de la cámara */
        {1, 0x14, F_ANG, 0}, {1, 0x16, F_ANG, 0}, {1, 0x18, F_ANG, 0},   /* ángulos (círculo = 16384) */
        {1, 0x9C, F_I32, 1}, {1, 0xA0, F_I32, 1}, {1, 0xA4, F_I32, 1},   /* vector de la cámara */
    };
    memcpy(FIELDS, cam, sizeof cam);
    for (int k = 0; k < NOBJ; k++)
        for (int j = 0; j < 3; j++) FIELDS[9 + k * 3 + j] = (RrField){0, 0x474 + k * 0x5C + j * 4, F_I32, 2 + k};
}

static const Ac3Version *g_v;
static const RrAddr *g_a;
static int g_busy;
static uint32_t g_st0, g_st1, g_sp;                          /* primitivas del estático; pila */
static uint8_t g_s0[PSX_RAM_SIZE], g_s0_scr[0x400], g_wt[PSX_RAM_SIZE / 32];
static PsxGte g_s0_gte;
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
static int g_have_cur, g_have_prev, g_complete;

static uint32_t field_addr(int i) {
    uint32_t gr = rd32(g_a->g_render);
    return FIELDS[i].base ? rd32(gr + 40) + (uint32_t)FIELDS[i].off : gr + (uint32_t)FIELDS[i].off;
}
static int32_t field_get(int i) {
    uint32_t a = field_addr(i);
    return FIELDS[i].kind == F_ANG ? (int32_t) * (int16_t *)psx_ptr(a) : (int32_t)rd32(a);
}
static void field_set(int i, int32_t v) {
    uint32_t a = field_addr(i);
    if (FIELDS[i].kind == F_ANG) *(int16_t *)psx_ptr(a) = (int16_t)v;
    else wr32(a, (uint32_t)v);
}

/* inicio del dibujo estático: S0 y el registro de escrituras */
static void mark0(RCpu *c) {
    if (g_busy) return;
    g_st0 = rd32(rd32(g_a->g_render) + 20);
    g_sp = c->r[29];
    memcpy(g_s0, psx_ram, sizeof g_s0);
    memcpy(g_s0_scr, psx_scratch, sizeof g_s0_scr);
    g_s0_gte = *(PsxGte *)psx_gte_get_context();
    memset(g_wt, 0, sizeof g_wt);
    mipsi_wtrace = g_wt;
    if (g_survey < 0) { g_survey = getenv("AC3_RR_SURVEY") != NULL; if (g_survey) atexit(survey_print); }
    if (g_survey) { memset(g_rt, 0, sizeof g_rt); mipsi_rtrace = g_rt; }
    if (g_have_cur) { memcpy(g_prev, g_cur, sizeof g_prev); g_have_prev = 1; }
    for (int i = 0; i < NFIELDS; i++) g_cur[i] = field_get(i);
    g_have_cur = 1;
    g_complete = 0;
}
/* fin (la función siguiente): rango de primitivas (sin el E5 que el juego pone justo antes) */
static void mark1(RCpu *c) {
    (void)c;
    if (g_busy) return;
    g_st1 = rd32(rd32(g_a->g_render) + 20) - 12;
    mipsi_wtrace = NULL;
    g_complete = g_st1 > g_st0;
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
    return a + (int32_t)((double)((int64_t)b - a) * t + 0.5);
}

int ac3_rr_build(uint32_t ot, float t) {
    if (!ac3_rr_ready()) return -1;
    /* corte de plano: la cámara salta (más de 1/8 de vuelta o de 2^21 en posición) */
    for (int i = 0; i < 6; i++) {
        int32_t d = g_cur[i] - g_prev[i];
        if (FIELDS[i].kind == F_ANG) { d &= 0x3FFF; if (d >= 0x2000) d -= 0x4000; if (d > 2048 || d < -2048) return -1; }
        else if (d > (1 << 21) || d < -(1 << 21)) return -1;
    }
    uint32_t gr = rd32(g_a->g_render), cam = rd32(gr + 40);
    /* 1. quitar de la cadena lo estático; anotar cabecera y cuerpo de cada paquete de la cadena */
    static uint8_t inchain[PSX_RAM_SIZE / 32];
    memset(inchain, 0, sizeof inchain);
    uint32_t a = ot, guard = 0;
    while ((a & 0xFFFFFF) != 0xFFFFFF && ++guard < 300000) {
        uint32_t ra = a & 0x1FFFFC, tag = rd32(a | 0x80000000u), next = tag & 0xFFFFFF;
        for (uint32_t w = 0; w <= (tag >> 24) && ra + 4 * w < PSX_RAM_SIZE; w++) {
            uint32_t q = ra + 4 * w;
            inchain[q >> 5] |= (uint8_t)(1u << ((q >> 2) & 7));
        }
        while (next != 0xFFFFFF && (next | 0x80000000u) >= g_st0 && (next | 0x80000000u) < g_st1)
            next = rd32(next | 0x80000000u) & 0xFFFFFF;
        wr32(a | 0x80000000u, (tag & 0xFF000000u) | next);
        a = next | 0x80000000u;
    }
    /* 2. volver a S0 en lo que el dibujo estático escribió (no en tablas de orden, primitivas ni
     *    paquetes de la cadena); las primitivas nuevas, detrás de todas las de esta imagen */
    uint32_t prim_end = rd32(gr + 20);
    uint32_t ex0 = rd32(gr + 24) & 0x1FFFFF, ex1 = (rd32(gr + 36) & 0x1FFFFF) + 0x1F800;
    /* toda palabra distinta de S0 (no solo las que escribió el dibujo: también la pila, que leen
     *    algunas funciones sin iniciarla y que otras han reescrito desde entonces) */
    for (uint32_t w = 0; w < PSX_RAM_SIZE; w += 4) {
        if (*(uint32_t *)(psx_ram + w) == *(uint32_t *)(g_s0 + w) || (w >= ex0 && w < ex1)) continue;
        if (inchain[w >> 5] >> ((w >> 2) & 7) & 1) continue;
        *(uint32_t *)(psx_ram + w) = *(uint32_t *)(g_s0 + w);
    }
    memcpy(psx_scratch, g_s0_scr, sizeof g_s0_scr);
    *(PsxGte *)psx_gte_get_context() = g_s0_gte;
    wr32(gr + 20, prim_end);
    /* 3. las entradas, a medio camino; un grupo (un objeto) cuya posición salta (otra ocupación de la
     *    ranura, aparece, desaparece) se queda como en el paso actual */
    for (int i = 0; i < NFIELDS;) {
        int j = i, jump = 0;
        while (j < NFIELDS && FIELDS[j].group == FIELDS[i].group) {
            int32_t d = g_cur[j] - g_prev[j];
            if (FIELDS[j].kind == F_I32 && (d > (1 << 20) || d < -(1 << 20))) jump = 1;
            j++;
        }
        for (int k = i; k < j; k++) field_set(k, t >= 1.0f || jump ? g_cur[k] : lerp_field(k, t));
        i = j;
    }
    /* 4. el dibujo estático del juego */
    g_busy = 1;
    guest_call(g_a->cam_matrix, cam, gr + 0x2C, gr + 0x4C);
    for (int i = 0; i < 4; i++) guest_call(g_a->draw[i], 0, 0, 0);
    g_busy = 0;
    /* 5. en cada cajón: lo de después del estático > lo redibujado > lo de antes (el juego inserta a
     *    la cabeza; lo redibujado entró el último) */
    uint32_t ob = rd32(gr + 16) & 0x1FFFFF, oe = ob + 0x4000, b = ot & 0xFFFFFF;
    static uint32_t seg[8192], ord[8192];
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
        uint32_t tail = q, no = 0;
        for (int cls = 0; cls < 3; cls++)
            for (uint32_t i = 0; i < nseg; i++) {
                uint32_t A = seg[i] | 0x80000000u;
                int c2 = A >= prim_end ? 1 : (A >= g_st1 && A < prim_end) ? 0 : 2;
                if (c2 == cls) ord[no++] = seg[i];
            }
        int changed = 0;
        for (uint32_t i = 0; i < nseg; i++) changed |= ord[i] != seg[i];
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
    return 0;
}
