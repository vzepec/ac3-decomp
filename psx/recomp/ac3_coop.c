/* Etapa 9, pasos 3-5: el segundo avion por cambio de contexto y su vista (ver ac3_coop.h). */
#include "ac3_coop.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __ANDROID__   /* (paso 6) en el APK, los mensajes al registro del sistema (logcat, como game_main.c) */
#include <android/log.h>
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "AC3Game", __VA_ARGS__)
#endif

#include "ac3_rerender.h"
#include "psx_gte.h"
#include "psx_libetc.h"
#include "psx_libgte.h"
#include "psx_mem.h"

typedef struct {
    const char *name;
    uint32_t ent_table;      /* puntero a la tabla de 64 punteros a entidad (la 0, el jugador) */
    uint32_t ctx[6];         /* punteros a los bloques del jugador (FUN_8006b7b4) */
    uint32_t after_player;   /* la funcion del paso que sigue al jugador (FUN_80044f70) */
    uint32_t player_step;    /* el paso del jugador (FUN_800715fc) */
    uint32_t pad_map;        /* mando -> entrada del juego (FUN_800288d8) */
    uint32_t heap_limit;     /* tope del asignador (FUN_800270c4) */
    uint32_t orders[5];      /* ordenes que cambian el modo del jugador (FUN_80074e9c...) */
    uint32_t aim, aim_ptrs, aim_vec;   /* la punteria del jugador (FUN_80044950), sus registros por entidad
                                        * (punteros en DAT_800C57F0) y sus globales (DAT_800C57C0, 0x14 bytes) */
    uint32_t effects;        /* la etapa de efectos (FUN_80083cfc): lo del jugador solo para la entidad 0 */
    uint32_t fx_player[4];   /* sus partes del jugador: FUN_80081ad0, FUN_80083bac, FUN_80083aac, FUN_8008eb78 */
    uint32_t fx_base, fx_wind;   /* DAT_800BB958 (estado de efectos), DAT_800BB878 */
    uint32_t view[4][2];     /* estado de la vista y la partida del jugador 1 que el paso del 2 no debe tocar
                              * (direccion, bytes); la camara y g_render aparte */
    uint32_t cam_step, cam_prep, cam_matrix;   /* (paso 5) el paso de la camara (FUN_80039f10: blanco), su ajuste
                                                * (FUN_8007a03c) y sus matrices (FUN_80033214) */
    uint32_t hud_proj, hud_lock;   /* la parte de logica del HUD: proyeccion de las entidades (FUN_8003adb4) y fijado
                                    * (FUN_8003e9a0) */
    uint32_t screen_ptrs;    /* los registros de pantalla de cada entidad (punteros en DAT_800C50C0) */
    uint32_t pview[6][2];    /* el estado del blanco y del fijado del jugador (direccion, bytes): uno por jugador */
    uint32_t mission_draw, noop;   /* el dibujo de la mision (FUN_80033910: la logica que recibe y luego el dibujo) y una
                                    * funcion que no hace nada (FUN_8003a2f8: devuelve -1) */
    uint32_t missile_launch, weapons;   /* el lanzamiento de un misil (FUN_800672d4) y el estado de las armas (DAT_800BBA60:
                                         * misiles en + (0x32 + k * 0x29) * 4, mascara en + 0x552 * 4) */
    uint32_t snd_rel;        /* (paso 6) la posicion de un sonido respecto al oyente (FUN_8002e488(salida, oyente, fuente)) */
    uint32_t radar_draw, blip_draw, radar_center, radar_range;   /* el dibujo del radar (FUN_8003d8c0: desde la ranura
                                                                   * 2), el de un punto (FUN_8003da58), su centro y giro
                                                                   * (*DAT_800BB978) y su alcance (DAT_800C569C) */
    uint32_t map_link, hud_counts, hud_sprites;   /* el mapa grande (cuadrado mantenido): FUN_8003c5f4 enlaza sus baldosas;
                                                   * su numero en *DAT_800BB970 + 0xC, ellas en *DAT_800BB974 + 0x518 */
} CoopAddr;
#define NORDERS 5

static const uint32_t CTX_SIZE[6] = {8, 0x10, 0x10, 0x70, 0x3C, 0x38};
#define ENT_SIZE 0x58      /* la entidad del jugador (las de los cazas, 0x5C) */
#define INPUT_OFF 0x120    /* entrada del juego en g_state */
#define INPUT_SIZE 0x40
#define PAD_OUT 0x50       /* salida de la ranura de mando 0 en g_state; 0x1A por ranura */
#define PAD_CFG 0x15C      /* configuracion de botones en g_state */
#define HEAP_PTR 0x1C      /* puntero del asignador en g_state */
#define G_RENDER 0x800BB81Cu   /* puntero a g_render (camara en g_render + 0x28) */
#define CAM_SIZE 0x200     /* la camara: modo (+0), objetivo (+0x34), entidad en la que va (+0xC4)... */

static const CoopAddr ADDR[] = {
    {"US", 0x800BB824u, {0x800BBA68u, 0x800BBA6Cu, 0x800BBA70u, 0x800BBA74u, 0x800BBA78u, 0x800BBA80u},
     0x80044F70u, 0x800715FCu, 0x800288D8u, 0x801FB700u,
     {0x80074E9Cu, 0x80074EB8u, 0x80074ED8u, 0x80074EF8u, 0x80074F18u},
     0x80044950u, 0x800C57F0u, 0x800C57C0u,
     0x80083CFCu, {0x80081AD0u, 0x80083BACu, 0x80083AACu, 0x8008EB78u}, 0x800BB958u, 0x800BB878u,
     /* 0x800BE60C: jugador derribado (0 -> 1 al caer el 2); 0x800BB868: canales de sonido del motor
      * (FUN_8003012c los apaga); 0x800C6508..14: copias del control (FUN_80070d34) */
     {{0x800BE60Cu, 4}, {0x800BB868u, 4}, {0, 0}, {0, 0}},
     0x80039F10u, 0x8007A03Cu, 0x80033214u, 0x8003ADB4u, 0x8003E9A0u, 0x800C50C0u,
     /* DAT_800C5680..9F: blanco elegido (568e), fijado (5692), colores...; DAT_800BB890: el recuadro del fijado (estado,
      * entidad, tamano, x, y); DAT_800BB960: pasos con el mismo blanco; DAT_800C57E0..EF; (paso 6) 0x800C6508..17: las
      * copias del control (FUN_80070d34; el horizonte de la cabina), antes protegidas como del jugador 1 */
     {{0x800C5680u, 0x20}, {0x800BB890u, 8}, {0x800BB960u, 4}, {0x800C57E0u, 0x10}, {0x800C6508u, 0x10}, {0, 0}},
     0x80033910u, 0x8003A2F8u, 0x800672D4u, 0x800BBA60u, 0x8002E488u, 0x8003D8C0u, 0x8003DA58u, 0x800BB978u, 0x800C569Cu,
     0x8003C5F4u, 0x800BB970u, 0x800BB974u},
};

static const CoopAddr *A;
static uint32_t g_gp, g_state_ptr;
static uint32_t g_mine[6], g_input2, g_alloc_end;
/* (paso 4) sus copias del fogonazo de la metralleta (8 bytes) y de la punteria (registros por entidad y
 * globales), en la RAM del juego como sus bloques (la huella y una instantanea las incluyen) */
#define GUNFX_SIZE 8
#define AIM_SIZE (64 * 0x34 + 0x14)
static uint32_t g_gunfx2, g_aim2;
/* (paso 5) su camara (0x200 bytes), su estado del blanco y del fijado (pview) y sus registros de pantalla */
#define PVIEW_MAX 0x80
#define SCR_REC 0xC
#define SCR_SIZE (64 * SCR_REC)
static uint32_t g_cam2, g_pview2, g_scr2;
#define BLIP_SIZE 0x10
static uint32_t g_blip;   /* (paso 6) el registro del punto del otro jugador en el radar */
static int g_busy, g_steps, g_orders, g_revives;
static int g_rebuilding;   /* (paso 5) dentro del redibujo del jugador 2 */
static uint32_t g_e1;

static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, psx_ptr(a), 4); return v; }
static void wr32(uint32_t a, uint32_t v) { memcpy(psx_ptr(a), &v, 4); }

/* (paso 4) la punteria (distancia y angulo de cada entidad al morro, fijado: FUN_80044950) es del jugador: los
 * registros de 0x34 bytes a los que apuntan DAT_800C57F0[k] (contiguos) y las globales de DAT_800C57C0. Los
 * del jugador 2 viven aparte y se intercambian con su contexto */
#define AIM_REC 0x34
static int aim_range(uint32_t *lo, uint32_t *hi) {
    *lo = 0xFFFFFFFFu; *hi = 0;
    for (int k = 0; k < 64; k++) {
        uint32_t p = rd32(A->aim_ptrs + 4 * k);
        if (!p) continue;
        if (p < *lo) *lo = p;
        if (p + AIM_REC > *hi) *hi = p + AIM_REC;
    }
    return *hi > *lo && *hi - *lo <= 64 * AIM_REC;
}
static void aim_swap(void) {
    uint32_t lo, hi;
    if (!aim_range(&lo, &hi)) return;
    uint8_t t[AIM_SIZE], *q = psx_ptr(g_aim2);
    uint32_t n = hi - lo;
    memcpy(t, psx_ptr(lo), n); memcpy(t + n, psx_ptr(A->aim_vec), 0x14);
    memcpy(psx_ptr(lo), q, n); memcpy(psx_ptr(A->aim_vec), q + n, 0x14);
    memcpy(q, t, n + 0x14);
}
static void aim_init(void) {
    uint32_t lo, hi;
    if (!aim_range(&lo, &hi)) return;
    memcpy(psx_ptr(g_aim2), psx_ptr(lo), hi - lo);
    memcpy(psx_ptr(g_aim2) + (hi - lo), psx_ptr(A->aim_vec), 0x14);
}
/* (paso 5) lo de la vista que es de cada jugador: la camara (g_render + 0x28 apunta a ella), el estado del blanco y del
 * fijado (pview) y los registros de pantalla de cada entidad (punteros en DAT_800C50C0, 0xC bytes, contiguos: la
 * proyeccion del HUD). Los del jugador 2 viven aparte y se intercambian con su contexto */
static int scr_range(uint32_t *lo, uint32_t *hi) {
    *lo = 0xFFFFFFFFu; *hi = 0;
    for (int k = 0; k < 64; k++) {
        uint32_t p = rd32(A->screen_ptrs + 4 * k);
        if (!p) continue;
        if (p < *lo) *lo = p;
        if (p + SCR_REC > *hi) *hi = p + SCR_REC;
    }
    return *hi > *lo && *hi - *lo <= SCR_SIZE;
}
static void xswap(uint32_t a, uint32_t b, uint32_t n) {
    uint8_t t[0x200], *p = psx_ptr(a), *q = psx_ptr(b);
    while (n) {
        uint32_t k = n < sizeof t ? n : sizeof t;
        memcpy(t, p, k); memcpy(p, q, k); memcpy(q, t, k);
        p += k; q += k; n -= k;
    }
}
static void view2_swap(void) {
    xswap(rd32(rd32(G_RENDER) + 0x28), g_cam2, 0x200);
    uint32_t o = 0;
    for (int i = 0; i < 6 && A->pview[i][1]; i++) { xswap(A->pview[i][0], g_pview2 + o, A->pview[i][1]); o += A->pview[i][1]; }
    uint32_t lo, hi;
    if (scr_range(&lo, &hi)) xswap(lo, g_scr2, hi - lo);
}
/* la primera vez y al reaparecer: como los del jugador 1, pero la camara sigue a su avion */
static void view2_init(uint32_t e0, uint32_t e1) {
    uint32_t cam = rd32(rd32(G_RENDER) + 0x28);
    memcpy(psx_ptr(g_cam2), psx_ptr(cam), 0x200);
    for (uint32_t o = 0; o < 0x200; o += 4) {   /* los punteros a la entidad 0, a la 1 (paso 6: tambien dentro de ella: el
                                                * objetivo +0x34 apunta a su posicion, e + 8; antes seguia al avion del 1) */
        uint32_t v = rd32(g_cam2 + o);
        if (v >= e0 && v < e0 + ENT_SIZE) wr32(g_cam2 + o, v - e0 + e1);
    }
    uint32_t o = 0;
    for (int i = 0; i < 6 && A->pview[i][1]; i++) { memcpy(psx_ptr(g_pview2 + o), psx_ptr(A->pview[i][0]), A->pview[i][1]); o += A->pview[i][1]; }
    uint32_t lo, hi;
    if (scr_range(&lo, &hi)) memcpy(psx_ptr(g_scr2), psx_ptr(lo), hi - lo);
}
/* llamada a una funcion del juego por debajo de la pila de quien llama */
static void guest_call(const RCpu *c, uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2) {
    RCpu cb;
    memset(&cb, 0, sizeof cb);
    cb.r[4] = a0; cb.r[5] = a1; cb.r[6] = a2;
    cb.r[28] = g_gp;
    cb.r[29] = (c->r[29] - 0x100u) & ~7u;
    cb.r[31] = 0x8000FFF0u;
    uint32_t ret = rc_ret;
    rc_ret = cb.r[31];
    rc_call(&cb, addr);
    rc_ret = ret;
}

/* prueba: la entidad e detras de la k (misma posicion y rumbo, AC3_COOP_DIST unidades atras, 150 si no; su velocidad) y
 * girada giro grados (prueba de la punteria: un misil recto falla). (paso 5) A menos de unas 250 unidades no se puede
 * fijar: la proyeccion del HUD desborda la division de la GTE (H / SZ) y el blanco no cuenta como en pantalla */
static void place_behind(uint32_t e, int k, const char *giro) {
    int dist = getenv("AC3_COOP_DIST") ? atoi(getenv("AC3_COOP_DIST")) : 150;
    uint32_t ek = rd32(rd32(A->ent_table) + 4 * (uint32_t)k);
    memcpy(psx_ptr(e + 8), psx_ptr(ek + 8), 12);
    memcpy(psx_ptr(e + 0x18), psx_ptr(ek + 0x18), 6);
    int16_t ry;
    memcpy(&ry, psx_ptr(ek + 0x1A), 2);
    double a = ry * (6.283185307179586 / 16384.0);
    wr32(e + 8, rd32(e + 8) - (uint32_t)(int32_t)(sin(a) * (dist << 12)));
    wr32(e + 0x10, rd32(e + 0x10) - (uint32_t)(int32_t)(cos(a) * (dist << 12)));
    memcpy(psx_ptr(e + 0x38), psx_ptr(ek + 0x38), 12);
    if (giro) {
        int16_t r = (int16_t)(ry + atoi(giro) * 16384 / 360);
        memcpy(psx_ptr(e + 0x1A), &r, 2);
    }
}

/* la primera vez en cada mision (la entidad 1 inactiva): sus bloques (con el asignador del juego,
 * si se reinicio desde la ultima reserva) como copia de los del jugador 1, y la entidad 1 como
 * copia de la 0, un poco por delante y a la derecha */
static int coop_init(uint32_t st, uint32_t e0, uint32_t e1) {
    uint32_t heap = rd32(st + HEAP_PTR);
    if (!g_alloc_end || heap < g_alloc_end) {
        uint32_t p = heap;
        for (int i = 0; i < 6; i++) { g_mine[i] = p; p += (CTX_SIZE[i] + 3) & ~3u; }
        g_input2 = p; p += INPUT_SIZE;
        g_gunfx2 = p; p += GUNFX_SIZE;
        g_aim2 = p; p += AIM_SIZE;
        g_cam2 = p; p += 0x200;
        g_pview2 = p; p += PVIEW_MAX;
        g_scr2 = p; p += SCR_SIZE;
        g_blip = p; p += BLIP_SIZE;
        if (p > A->heap_limit) { printf("coop: sin memoria en el asignador del juego\n"); return -1; }
        wr32(st + HEAP_PTR, p);
        g_alloc_end = p;
        printf("coop: bloques del jugador 2 en %08x-%08x\n", g_mine[0], p);
    }
    for (int i = 0; i < 6; i++) memcpy(psx_ptr(g_mine[i]), psx_ptr(rd32(A->ctx[i])), CTX_SIZE[i]);
    memcpy(psx_ptr(g_input2), psx_ptr(st + INPUT_OFF), INPUT_SIZE);
    memset(psx_ptr(g_gunfx2), 0, GUNFX_SIZE);
    aim_init();
    view2_init(e0, e1);
    memcpy(psx_ptr(e1), psx_ptr(e0), ENT_SIZE);
    wr32(e1, (rd32(e0) & ~0x3Fu) | 1u);   /* ranura 1 */
    static int dx = -1, dz;
    if (dx < 0) {   /* AC3_COOP_POS=dx,dz: donde aparece, en unidades del mundo respecto al jugador 1 */
        dx = 60; dz = 120;
        if (getenv("AC3_COOP_POS")) sscanf(getenv("AC3_COOP_POS"), "%d,%d", &dx, &dz);
    }
    wr32(e1 + 8, rd32(e0 + 8) + ((uint32_t)dx << 12));
    wr32(e1 + 0x10, rd32(e0 + 0x10) + ((uint32_t)dz << 12));
    if (getenv("AC3_COOP_TRAS")) place_behind(e1, atoi(getenv("AC3_COOP_TRAS")), getenv("AC3_COOP_GIRO"));
    if (getenv("AC3_COOP_TRAS1")) place_behind(e0, atoi(getenv("AC3_COOP_TRAS1")), getenv("AC3_COOP_GIRO1"));   /* (paso 5) el 1 */
    printf("coop: entidad 1 (%08x) activa en el VBlank %d\n", e1, VSync(-1));
    return 0;
}

/* el contexto del jugador 2 en lugar del 1: tabla de entidades [0] <-> [1], los seis bloques y la entrada */
/* (paso 4) el estado del fogonazo de la metralleta (DAT_800BB958 + 0x3948: contador y tipo; lo pone
 * FUN_8008eb40 al disparar) es uno: el del jugador 2 vive aparte y se intercambia con su contexto */
#define FX_GUN 0x3948
static void gunfx_swap(void) {
    uint8_t t[GUNFX_SIZE], *p = psx_ptr(rd32(A->fx_base) + FX_GUN), *q = psx_ptr(g_gunfx2);
    memcpy(t, p, GUNFX_SIZE); memcpy(p, q, GUNFX_SIZE); memcpy(q, t, GUNFX_SIZE);
}
typedef struct { uint32_t st, tab, e0, e1, keep[6]; uint8_t in1[INPUT_SIZE]; } Swap;
static void swap_in(Swap *s) {
    s->st = rd32(g_state_ptr); s->tab = rd32(A->ent_table);
    s->e0 = rd32(s->tab); s->e1 = rd32(s->tab + 4);
    for (int i = 0; i < 6; i++) { s->keep[i] = rd32(A->ctx[i]); wr32(A->ctx[i], g_mine[i]); }
    memcpy(s->in1, psx_ptr(s->st + INPUT_OFF), INPUT_SIZE);
    memcpy(psx_ptr(s->st + INPUT_OFF), psx_ptr(g_input2), INPUT_SIZE);
    wr32(s->tab, s->e1);
    wr32(s->tab + 4, s->e0);
    gunfx_swap();
    aim_swap();
}
static void swap_out(const Swap *s) {
    aim_swap();
    gunfx_swap();
    wr32(s->tab, s->e0);
    wr32(s->tab + 4, s->e1);
    memcpy(psx_ptr(g_input2), psx_ptr(s->st + INPUT_OFF), INPUT_SIZE);
    memcpy(psx_ptr(s->st + INPUT_OFF), s->in1, INPUT_SIZE);
    for (int i = 0; i < 6; i++) wr32(A->ctx[i], s->keep[i]);
}
/* (paso 4) lo que el paso del jugador 2 escribe y es de la vista o de la partida del 1: al caer, la camara se
 * iba a sus restos (modo 0x501, objetivo la entidad 1), g_render + 0 pasaba de 2 a 3 y 0x800BE60C a 1
 * (jugador derribado) y se apagaban los canales del motor del 1; en cada paso, sus copias del control
 * (0x800C6508) y g_render + 0x28C. Se guardan antes y vuelven despues (AC3_COOP_DIFF lo mide) */
typedef struct { uint8_t gr0[4], gr28c[8], v[4][16]; uint32_t gr; } ViewKeep;
static void view_save(ViewKeep *k) {
    k->gr = rd32(G_RENDER);
    memcpy(k->gr0, psx_ptr(k->gr), 4);
    memcpy(k->gr28c, psx_ptr(k->gr + 0x28C), 8);
    for (int i = 0; i < 4 && A->view[i][1]; i++) memcpy(k->v[i], psx_ptr(A->view[i][0]), A->view[i][1]);
}
static void view_restore(const ViewKeep *k) {
    memcpy(psx_ptr(k->gr), k->gr0, 4);
    memcpy(psx_ptr(k->gr + 0x28C), k->gr28c, 8);
    for (int i = 0; i < 4 && A->view[i][1]; i++) memcpy(psx_ptr(A->view[i][0]), k->v[i], A->view[i][1]);
}

/* el juego pasa a ser del jugador 2 (p2_begin) y vuelve al 1 (p2_end) */
static int keep_view(void) {
    static int on = -1;   /* AC3_COOP_VISTA1=0 (prueba): sin proteger la vista del jugador 1 */
    if (on < 0) on = !getenv("AC3_COOP_VISTA1") || atoi(getenv("AC3_COOP_VISTA1")) != 0;
    return on;
}
static ViewKeep g_vk;
static void p2_begin(Swap *s) {
    if (keep_view()) { view_save(&g_vk); view2_swap(); }   /* (paso 5) su camara y su blanco, en lugar de los del 1 */
    swap_in(s);
    g_busy = 1;
}
static void p2_end(const Swap *s) {
    g_busy = 0;
    swap_out(s);
    if (keep_view()) { view2_swap(); view_restore(&g_vk); }
}
/* llamar a una funcion del juego como el jugador 2 */
static void as_p2(const RCpu *c, uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2) {
    Swap s;
    p2_begin(&s);
    guest_call(c, addr, a0, a1, a2);
    p2_end(&s);
}
/* el jugador 2 existe en esta mision: entidad 1 activa y sus bloques reservados tras el ultimo reinicio
 * del asignador */
static int p2_active(void) {
    if (!A || !g_alloc_end) return 0;
    uint32_t st = rd32(g_state_ptr), e1 = rd32(rd32(A->ent_table) + 4);
    return (rd32(e1) & ~0x3Fu) && rd32(st + HEAP_PTR) >= g_alloc_end;
}

/* (paso 6) el jugador 2 tambien en el sitio de la entidad 0 (g_render + 0x3BC): el dibujo estatico dibuja "el avion del
 * jugador" (y el origen de las estelas) desde esa direccion fija, no desde la tabla, y la camara exterior lo sigue. Se intercambia el
 * contenido de las dos entidades (cada una con su ranura en las banderas), la tabla vuelve a su orden y los punteros
 * de la camara (la del 2, ya en su sitio) a una y otra se cambian. Dos veces lo deshace */
static void ent_content_swap(uint32_t tab, uint32_t e0, uint32_t e1) {
    uint8_t t[ENT_SIZE];
    uint32_t f0 = rd32(e0), f1 = rd32(e1);
    memcpy(t, psx_ptr(e0), ENT_SIZE); memcpy(psx_ptr(e0), psx_ptr(e1), ENT_SIZE); memcpy(psx_ptr(e1), t, ENT_SIZE);
    wr32(e0, (rd32(e0) & ~0x3Fu) | (f0 & 0x3Fu));
    wr32(e1, (rd32(e1) & ~0x3Fu) | (f1 & 0x3Fu));
    uint32_t a = rd32(tab), b = rd32(tab + 4);
    wr32(tab, b); wr32(tab + 4, a);
    uint32_t cam = rd32(rd32(G_RENDER) + 0x28);
    for (uint32_t o = 0; o < CAM_SIZE; o += 4) {
        uint32_t v = rd32(cam + o);
        if (v >= e0 && v < e0 + ENT_SIZE) wr32(cam + o, v - e0 + e1);
        else if (v >= e1 && v < e1 + ENT_SIZE) wr32(cam + o, v - e1 + e0);
    }
}
/* AC3_COOP_CONTENIDO=mascara (prueba): donde se hace (1: el redibujo, 2: su vista en la simulacion, 4: su paso) */
static int content_mask(void) {
    static int m = -1;
    if (m < 0) m = getenv("AC3_COOP_CONTENIDO") ? atoi(getenv("AC3_COOP_CONTENIDO")) : 3;
    return m;
}
static void log_step(uint32_t e0, uint32_t e1) {
    static int log_every = -1;   /* AC3_COOP_LOG=n: cada n pasos, los dos aviones */
    if (log_every < 0) log_every = getenv("AC3_COOP_LOG") ? atoi(getenv("AC3_COOP_LOG")) : 0;
    if (log_every <= 0 || g_steps % log_every) return;
    for (int k = 0; k < 2; k++) {
        uint32_t e = k ? e1 : e0, a = k ? g_mine[3] : rd32(A->ctx[3]);
        int16_t ang[3], hp;
        memcpy(ang, psx_ptr(e + 0x18), 6);
        memcpy(&hp, psx_ptr(e + 0x4E), 2);
        printf("coop: VBlank %d jugador %d pos %d %d %d ang %d %d %d vel %d %d %d vida %d banderas %08x modo %x %x\n", VSync(-1), k + 1,
               (int32_t)rd32(e + 8) >> 12, (int32_t)rd32(e + 0xC) >> 12, (int32_t)rd32(e + 0x10) >> 12, ang[0], ang[1], ang[2],
               (int32_t)rd32(e + 0x38), (int32_t)rd32(e + 0x3C), (int32_t)rd32(e + 0x40), hp, rd32(e), rd32(a), rd32(a + 4));
    }
}

static void after_player(RCpu *c) {
    if (g_busy) return;
    uint32_t st = rd32(g_state_ptr), tab = rd32(A->ent_table);
    uint32_t e0 = rd32(tab), e1 = rd32(tab + 4);
    static int from = -1;   /* AC3_COOP_DESDE=v (prueba): el jugador 2 aparece desde ese VBlank */
    if (from < 0) from = getenv("AC3_COOP_DESDE") ? atoi(getenv("AC3_COOP_DESDE")) : 0;
    if (VSync(-1) < from) return;
    if (!(rd32(e1) & ~0x3Fu) && coop_init(st, e0, e1)) return;
    g_e1 = e1;
    /* (paso 4) derribado (vida < 1): pasados AC3_COOP_REVIVE pasos (150, 5 s) reaparece junto al jugador 1, como
     * al empezar; mientras, su paso sigue (los restos) */
    static int revive = -1, down;
    if (revive < 0) revive = getenv("AC3_COOP_REVIVE") ? atoi(getenv("AC3_COOP_REVIVE")) : 150;
    int16_t hp2, hp1;
    memcpy(&hp2, psx_ptr(e1 + 0x4E), 2);
    memcpy(&hp1, psx_ptr(e0 + 0x4E), 2);
    if (hp2 < 1) down++; else down = 0;
    if (down > revive && hp1 > 0) {
        printf("coop: el jugador 2 reaparece en el VBlank %d\n", VSync(-1));
        if (coop_init(st, e0, e1)) return;
        down = 0;
        g_revives++;
    }
    static uint32_t probe_fn, probe_vb;   /* AC3_COOP_SONDA=funcion,vblank (medicion): que escribe esa funcion
                                            * llamada como el jugador 2; despues la RAM vuelve a estar como antes */
    if (!probe_vb && getenv("AC3_COOP_SONDA")) sscanf(getenv("AC3_COOP_SONDA"), "%x,%u", &probe_fn, &probe_vb);
    if (probe_vb && (uint32_t)VSync(-1) >= probe_vb && probe_fn) {
        static uint8_t ram0[PSX_RAM_SIZE], scr0[0x400];
        memcpy(ram0, psx_ram, PSX_RAM_SIZE); memcpy(scr0, psx_scratch, 0x400);
        as_p2(c, probe_fn, 0, 0, 0);
        uint32_t lo = (c->r[29] - 0x2000u) & 0x1FFFFFu;
        int n = 0;
        uint32_t run = 0, run_start = 0;
        printf("coop: sonda %08x en el VBlank %d escribe:", probe_fn, VSync(-1));
        for (uint32_t a = 0; a <= PSX_RAM_SIZE; a += 4) {
            int d = a < PSX_RAM_SIZE && memcmp(ram0 + a, psx_ram + a, 4) && !(a >= lo && a < 0x200000u);
            if (d && !run) run_start = a;
            if (d) run++;
            else if (run) { if (n++ < 60) printf(" %08x+%x", 0x80000000u | run_start, run * 4); run = 0; }
        }
        printf(" (%d tramos)\n", n);
        memcpy(psx_ram, ram0, PSX_RAM_SIZE); memcpy(psx_scratch, scr0, 0x400);
        probe_fn = 0;
    }
    /* el mando del puerto 2 (ranura 4) en la entrada del jugador 2 */
    guest_call(c, A->pad_map, st + PAD_OUT + 4 * 0x1A, g_input2, st + PAD_CFG);
    static int d0 = -2, d1;   /* AC3_COOP_DIFF=v0,v1 (medicion): lo que el paso del jugador 2 cambia fuera de lo suyo */
    if (d0 == -2) { d0 = -1; if (getenv("AC3_COOP_DIFF")) sscanf(getenv("AC3_COOP_DIFF"), "%d,%d", &d0, &d1); }
    static uint8_t before[PSX_RAM_SIZE];
    int diff = d0 >= 0 && VSync(-1) >= d0 && VSync(-1) <= d1;
    if (diff) memcpy(before, psx_ram, PSX_RAM_SIZE);
    {   /* su punteria y su paso */
        Swap s;
        p2_begin(&s);
        static int aim_on = -1;   /* AC3_COOP_PUNTERIA=0 (prueba): el jugador 2 sin punteria propia */
        if (aim_on < 0) aim_on = !getenv("AC3_COOP_PUNTERIA") || atoi(getenv("AC3_COOP_PUNTERIA")) != 0;
        if (aim_on) guest_call(c, A->aim, 0, 0, 0);
        static int aim_log = -1;   /* AC3_COOP_PUNTERIA_LOG=k (medicion): banderas, distancia y angulo de la entidad k */
        if (aim_log < 0) aim_log = getenv("AC3_COOP_PUNTERIA_LOG") ? atoi(getenv("AC3_COOP_PUNTERIA_LOG")) : 0;
        if (aim_log > 0) {
            uint32_t r = rd32(A->aim_ptrs + 4 * (uint32_t)aim_log);
            int16_t ang;
            memcpy(&ang, psx_ptr(r + 0x18), 2);
            if (r) printf("coop: VBlank %d punteria del 2 sobre %d: banderas %08x distancia %d angulo %d\n", VSync(-1), aim_log,
                          rd32(r), rd32(r + 4), ang);
        }
        uint32_t tab = rd32(A->ent_table), pe0 = rd32(tab + 4), pe1 = rd32(tab);
        if (content_mask() & 4) ent_content_swap(tab, pe0, pe1);
        guest_call(c, A->player_step, 0, 0, 0);
        if (content_mask() & 4) ent_content_swap(tab, pe0, pe1);
        p2_end(&s);
    }
    if (diff) {
        uint32_t lo = (c->r[29] - 0x2000u) & 0x1FFFFFu, hi = 0x200000u;   /* la pila de la llamada */
        int n = 0;
        printf("coop: VBlank %d, el paso del jugador 2 cambia:", VSync(-1));
        for (uint32_t a = 0; a < PSX_RAM_SIZE && n < 200; a += 4) {
            if (!memcmp(before + a, psx_ram + a, 4)) continue;
            uint32_t A32 = 0x80000000u | a;
            if (A32 >= e1 && A32 < e1 + ENT_SIZE) continue;
            if (A32 >= g_mine[0] && A32 < g_alloc_end) continue;
            if (a >= lo && a < hi) continue;
            uint32_t b0, b1;
            memcpy(&b0, before + a, 4); memcpy(&b1, psx_ram + a, 4);
            printf(" %08x:%08x>%08x", A32, b0, b1);
            n++;
        }
        printf("\n");
    }
    g_steps++;
    log_step(e0, e1);
}

/* (paso 5) la vista del jugador 2 en la simulacion: lo que para el 1 hacen el final del paso de vuelo (el paso de la
 * camara: el blanco elegido, FUN_80039f10) y la logica del HUD del dibujo (proyeccion de las entidades y fijado; sin ella
 * sus misiles no persiguen), con su camara y su estado. Lo que el HUD anade a la tabla de orden y a las primitivas se
 * descarta; las matrices de la camara del 1 y la GTE vuelven a estar como antes. a1: los argumentos del paso de la
 * camara del 1 (6 palabras en su pila; las del 2, justo debajo de la pila de quien llama) */
static void p2_view(const RCpu *c, uint32_t a1) {
    uint32_t gr = rd32(G_RENDER), blk1 = rd32(A->ctx[4]), blk2 = g_mine[4];
    uint32_t args = (c->r[29] - 0x40u) & ~7u, ot = rd32(gr + 0x10), prim = rd32(gr + 0x14);
    static uint8_t ot_keep[0x4000];
    uint8_t mat[0x40];
    PsxGte *gc = (PsxGte *)psx_gte_get_context(), gte = *gc;
    memcpy(psx_ptr(args), psx_ptr(a1), 0x18);
    wr32(args, rd32(blk2 + 0x1C));
    if (rd32(a1 + 4) == rd32(blk1 + 8) && rd32(a1 + 8) == rd32(blk1 + 0xC) && rd32(a1 + 0xC) == rd32(blk1 + 0x10))
        for (int i = 1; i < 4; i++) wr32(args + 4 * i, rd32(blk2 + 4 + 4 * i));   /* +8, +0xC, +0x10 (FUN_80032e70) */
    memcpy(mat, psx_ptr(gr + 0x2C), sizeof mat);
    memcpy(ot_keep, psx_ptr(ot), sizeof ot_keep);
    Swap s;
    p2_begin(&s);
    uint32_t tab = rd32(A->ent_table), pe0 = rd32(tab + 4), pe1 = rd32(tab);
    if (content_mask() & 2) ent_content_swap(tab, pe0, pe1);
    uint32_t cam = rd32(gr + 0x28);
    guest_call(c, A->cam_step, args, 0, 0);
    wr32(cam + 0xBC, rd32(args + 0x10));
    wr32(blk2 + 0x18, rd32(args + 0x14));
    guest_call(c, A->cam_prep, cam, 0, 0);
    guest_call(c, A->cam_matrix, cam, gr + 0x2C, gr + 0x4C);
    guest_call(c, A->hud_proj, 0, 0, 0);
    guest_call(c, A->hud_lock, 0, 0, 0);
    if (content_mask() & 2) ent_content_swap(tab, pe0, pe1);
    p2_end(&s);
    memcpy(psx_ptr(ot), ot_keep, sizeof ot_keep);
    wr32(gr + 0x14, prim);
    memcpy(psx_ptr(gr + 0x2C), mat, sizeof mat);
    *gc = gte;
}
/* medicion: los tramos de la RAM que cambian desde ram0, fuera de la pila, de lo propio del jugador 2 y de las tablas de
 * orden y primitivas */
static void print_writes(const RCpu *c, const uint8_t *ram0, const char *what) {
    uint32_t lo = (c->r[29] - 0x2000u) & 0x1FFFFFu, gr = rd32(G_RENDER);
    uint32_t x0 = rd32(gr + 0x18) & 0x1FFFFFu, x1 = (rd32(gr + 0x24) & 0x1FFFFFu) + 0x1F800;
    int n = 0;
    uint32_t run = 0, run_start = 0;
    printf("coop: %s en el VBlank %d escribe:", what, VSync(-1));
    for (uint32_t a = 0; a <= PSX_RAM_SIZE; a += 4) {
        uint32_t A32 = 0x80000000u | a;
        int d = a < PSX_RAM_SIZE && memcmp(ram0 + a, psx_ram + a, 4) && !(a >= lo && a < 0x200000u) && !(a >= x0 && a < x1) &&
                !(A32 >= g_mine[0] && A32 < g_alloc_end);
        if (d && !run) run_start = a;
        if (d) run++;
        else if (run) { if (n++ < 60) printf(" %08x+%x", 0x80000000u | run_start, run * 4); run = 0; }
    }
    printf(" (%d tramos)\n", n);
}
static uint32_t g_draw_sp;   /* (paso 5) la pila del paso de vuelo de este VBlank (0: sin paso; no se redibuja) */
static void camera_hook(RCpu *c) {
    if (g_busy || !p2_active()) return;
    g_draw_sp = c->r[29];
    static int on = -1, probe = -2;   /* AC3_COOP_VISTA=0 (prueba): sin la vista del 2 en la simulacion;
                                       * AC3_COOP_SONDA_VISTA=v (medicion): que escribe en el VBlank v */
    if (on < 0) on = !getenv("AC3_COOP_VISTA") || atoi(getenv("AC3_COOP_VISTA")) != 0;
    if (probe == -2) probe = getenv("AC3_COOP_SONDA_VISTA") ? atoi(getenv("AC3_COOP_SONDA_VISTA")) : -1;
    if (getenv("AC3_COOP_DBGHUD")) {   /* depuracion: la proyeccion del HUD del 1 sobre la entidad k */
        int k = atoi(getenv("AC3_COOP_DBGHUD"));
        uint32_t r = rd32(A->aim_ptrs + 4 * (uint32_t)k), cam = rd32(rd32(G_RENDER) + 0x28), e0 = rd32(rd32(A->ent_table)),
                 ek = rd32(rd32(A->ent_table) + 4 * (uint32_t)k), sr = rd32(A->screen_ptrs + 4 * (uint32_t)k);
        printf("coop: VBlank %d hud1 %d: f20 %08x f24 %d camara %d %d %d avion %d %d %d blanco %d %d %d pantalla %04x %d %d\n", VSync(-1), k,
               r ? rd32(r + 0x20) : 0, r ? rd32(r + 0x24) : 0, (int32_t)rd32(cam + 4) >> 12, (int32_t)rd32(cam + 8) >> 12,
               (int32_t)rd32(cam + 0xC) >> 12, (int32_t)rd32(e0 + 8) >> 12, (int32_t)rd32(e0 + 0xC) >> 12, (int32_t)rd32(e0 + 0x10) >> 12,
               (int32_t)rd32(ek + 8) >> 12, (int32_t)rd32(ek + 0xC) >> 12, (int32_t)rd32(ek + 0x10) >> 12,
               sr ? *(uint16_t *)psx_ptr(sr + 6) : 0, sr ? *(int16_t *)psx_ptr(sr + 8) : 0, sr ? *(int16_t *)psx_ptr(sr + 10) : 0);
    }
    if (getenv("AC3_COOP_DBGCAM")) {   /* depuracion: angulos (+0x14..0x18) y modo de las dos camaras */
        uint32_t cam = rd32(rd32(G_RENDER) + 0x28);
        int16_t a1[3], a2[3];
        memcpy(a1, psx_ptr(cam + 0x14), 6); memcpy(a2, psx_ptr(g_cam2 + 0x14), 6);
        printf("coop: VBlank %d camara 1: modo %08x ang %d %d %d | camara 2: modo %08x ang %d %d %d\n", VSync(-1), rd32(cam), a1[0], a1[1], a1[2],
               rd32(g_cam2), a2[0], a2[1], a2[2]);
    }
    if (getenv("AC3_COOP_DBGMISIL")) {   /* depuracion: los misiles activos (DAT_800BBA60 + (0x32 + k * 0x29) * 4) */
        uint32_t b = rd32(A->weapons), mask = rd32(b + 0x552 * 4);
        for (int k = 0; k < 32; k++) {
            if (!(mask >> k & 1)) continue;
            uint32_t m = b + (0x32u + (uint32_t)k * 0x29u) * 4;
            printf("coop: VBlank %d misil %d: banderas %08x dueno %08x blanco %d pos %d %d %d\n", VSync(-1), k, rd32(m), rd32(m + 0x6C),
                   (int32_t)rd32(m + 0x70), (int32_t)rd32(m + 0x1C) >> 12, (int32_t)rd32(m + 0x20) >> 12, (int32_t)rd32(m + 0x24) >> 12);
        }
    }
    if (!on) return;
    static uint8_t ram0[PSX_RAM_SIZE];
    int pr = probe >= 0 && VSync(-1) >= probe;
    if (pr) memcpy(ram0, psx_ram, PSX_RAM_SIZE);
    p2_view(c, c->r[4]);
    if (pr) { print_writes(c, ram0, "la vista del 2"); probe = -1; }
    static int log_every = -1;   /* AC3_COOP_BLANCO=n (medicion): cada n pasos, el blanco y el fijado de cada jugador */
    if (log_every < 0) log_every = getenv("AC3_COOP_BLANCO") ? atoi(getenv("AC3_COOP_BLANCO")) : 0;
    if (log_every > 0 && g_steps % log_every == 0) {
        const uint8_t *v1 = psx_ptr(A->pview[0][0]), *v2 = psx_ptr(g_pview2), *l1 = psx_ptr(A->pview[1][0]), *l2 = psx_ptr(g_pview2 + 0x20);
        printf("coop: VBlank %d blanco 1: %02x fijado %02x recuadro %d/%02x | blanco 2: %02x fijado %02x recuadro %d/%02x | +0x18 %d %d\n",
               VSync(-1), v1[0xE], v1[0x12], l1[0], l1[1], v2[0xE], v2[0x12], l2[0], l2[1], (int32_t)rd32(rd32(A->ctx[4]) + 0x18),
               (int32_t)rd32(g_mine[4] + 0x18));
    }
}

/* (paso 4) las ordenes de la mision que cambian el modo del jugador (piloto automatico del inicio, control,
 * fin de mision...: FUN_8003703c, FUN_80059e1c...) solo escriben el bloque del jugador 1: al entrar en una,
 * se ejecuta antes para el jugador 2 con los mismos argumentos */
static void order_hook(RCpu *c, int k) {
    if (g_busy || !p2_active()) return;
    as_p2(c, A->orders[k], c->r[4], c->r[5], c->r[6]);
    g_orders++;
}
/* (paso 4) al entrar en la etapa de efectos: sus partes del jugador, tambien para el 2 (como FUN_80083cfc
 * para la entidad 0): FUN_80081ad0 con su posicion y velocidad; si vive, FUN_80083bac (segun mision y avion)
 * y, en la mision 0x11, FUN_80083aac; y el fogonazo de su metralleta (FUN_8008eb78 con su estado) */
static void effects_hook(RCpu *c) {
    if (g_busy || !p2_active()) return;
    uint32_t st = rd32(g_state_ptr), gr = rd32(G_RENDER), e1 = rd32(rd32(A->ent_table) + 4);
    int16_t hp2, mission, plane;
    memcpy(&hp2, psx_ptr(e1 + 0x4E), 2);
    memcpy(&mission, psx_ptr(st + 0x390), 2);
    memcpy(&plane, psx_ptr(st + 0x392), 2);
    Swap s;
    p2_begin(&s);
    if (rd32(gr) & 2) {
        guest_call(c, A->fx_player[0], e1 + 8, e1 + 0x38, (uint32_t)-(int32_t)rd32(rd32(A->fx_wind) + 4));
        if (hp2 > 0) guest_call(c, A->fx_player[1], (uint32_t)(int32_t)mission, (uint32_t)(int32_t)plane, 0);
    }
    if (mission == 0x11 && hp2 > 0) guest_call(c, A->fx_player[2], e1 + 8, e1 + 0x38, 0);
    guest_call(c, A->fx_player[3], rd32(A->fx_base) + FX_GUN, 0, 0);
    p2_end(&s);
}

#define ORDER_HOOK(k) static void order_hook##k(RCpu *c) { order_hook(c, k); }
ORDER_HOOK(0) ORDER_HOOK(1) ORDER_HOOK(2) ORDER_HOOK(3) ORDER_HOOK(4)
static void (*const ORDER_HOOKS[NORDERS])(RCpu *) = {order_hook0, order_hook1, order_hook2, order_hook3, order_hook4};

/* (paso 6) el oyente: los sonidos con posicion (explosiones, motores cercanos: FUN_8002f16c / f20c y el gestor de
 * emisores FUN_8002f758) se calculan respecto a la camara del jugador 1 (oyente = camara + 4), asi que los impactos del
 * 2 lejos del 1 no se oian. El oyente de cada sonido pasa a ser la camara del jugador mas cercano a el. El sonido es parte
 * de la partida (el gestor da y quita voces segun la posicion, y el juego lee el estado de las voces): un oyente por
 * aparato separaria las partidas; esta regla es la misma en los dos. AC3_COOP_OYENTE=0 (prueba): como el juego */
static int64_t dist2(uint32_t a, uint32_t b) {
    int64_t s = 0;
    for (int i = 0; i < 3; i++) { int64_t d = ((int32_t)rd32(a + 4 * i) >> 12) - ((int32_t)rd32(b + 4 * i) >> 12); s += d * d; }
    return s;
}
static void listener_hook(RCpu *c) {
    if (g_busy || !p2_active()) return;
    static int on = -1;
    if (on < 0) on = !getenv("AC3_COOP_OYENTE") || atoi(getenv("AC3_COOP_OYENTE")) != 0;
    uint32_t l1 = rd32(rd32(G_RENDER) + 0x28) + 4, l2 = g_cam2 + 4, src = c->r[6];
    if (!on || c->r[5] != l1 || src < 0x80000000u) return;
    if (dist2(src, l2) < dist2(src, l1)) {
        c->r[5] = l2;
        if (getenv("AC3_COOP_DBGOYENTE"))   /* depuracion: los sonidos que se oyen desde el 2, y desde donde se piden */
            printf("coop: VBlank %d sonido desde la camara del 2 (a %lld; de la del 1, %lld) pedido desde %08x\n", VSync(-1),
                   (long long)dist2(src, l2), (long long)dist2(src, l1), c->r[31]);
    }
}

/* (paso 6) el radar: su dibujo (FUN_8003d8c0) y su logica (FUN_8003d46c) van de la ranura 2 a la 63, sin los dos
 * jugadores. Al entrar en su dibujo, el otro jugador (la ranura 1 de la tabla: el 2 en la imagen del 1; el 1 en el
 * redibujo del 2) como un punto azul mas, con su registro calculado como el juego (posicion respecto al centro y giro
 * del radar, rumbo, si va por encima o por debajo) y dibujado con la funcion del juego. En los dos aparatos igual.
 * AC3_COOP_RADAR=0 (prueba): como el juego */
static void radar_hook(RCpu *c) {
    if ((g_busy && !g_rebuilding) || !p2_active()) return;
    static int on = -1;
    if (on < 0) on = !getenv("AC3_COOP_RADAR") || atoi(getenv("AC3_COOP_RADAR")) != 0;
    uint32_t tab = rd32(A->ent_table), me = rd32(tab), other = rd32(tab + 4), rc = rd32(A->radar_center);
    int32_t range = (int32_t)rd32(A->radar_range);
    int16_t hp;
    memcpy(&hp, psx_ptr(other + 0x4E), 2);
    if (!on || !rc || range <= 0 || (~rd32(other) & 0x4000) || hp < 1) return;
    int32_t dx = ((int32_t)rd32(other + 8) >> 12) - (int32_t)rd32(rc), dz = (int32_t)rd32(rc + 4) - ((int32_t)rd32(other + 0x10) >> 12);
    int32_t cs = (int32_t)rd32(rc + 8), sn = (int32_t)rd32(rc + 12);
    int32_t x = ((dx * sn + dz * cs) >> 12 << 5) / range, y = ((-dx * cs + dz * sn) >> 12 << 5) / range;
    if ((uint32_t)(x + 0x1F) >= 0x3F || (uint32_t)(y + 0x1F) >= 0x3F) return;
    uint8_t *b = psx_ptr(g_blip);
    memset(b, 0, BLIP_SIZE);
    b[0] = range > 0x7FFF ? 9 : 1;   /* visible; con el alcance del mapa grande, como los demas puntos */
    b[2] = 2;                         /* azul */
    int32_t dy = ((int32_t)rd32(me + 0xC) - (int32_t)rd32(other + 0xC)) >> 12;
    b[5] = dy > 0x1000 ? 2 : dy < -0x1000 ? 0 : 1;
    int16_t r1, r2;
    memcpy(&r1, psx_ptr(other + 0x1A), 2); memcpy(&r2, psx_ptr(me + 0x1A), 2);
    b[4] = (uint8_t)((((int32_t)r1 - r2 + 0x200) & 0x3FFF) >> 10);
    b[6] = (uint8_t)(x + 0x2C);
    b[7] = (uint8_t)(y + 0x2E);
    guest_call(c, A->blip_draw, g_blip, 0, 0);
}

/* (paso 6) el mapa grande (cuadrado mantenido): FUN_8003e0fc pone una baldosa de 2x2 por entidad (una sombra y luego el
 * color: azul las del bando del jugador, bandera 0x40), tambien desde la ranura 2, en las primitivas de *DAT_800BB974 +
 * 0x518 (0x10 bytes; su numero en *DAT_800BB970 + 0xC), y FUN_8003c5f4 las enlaza. Al entrar en esta, la del otro
 * jugador, con la misma cuenta (FUN_8003e66c: x >> 23 y -z >> 23 en una rejilla de 256, ventana de 128 alrededor del
 * jugador) */
static int map_cell(uint32_t e, int *x, int *y) {
    int a = ((int32_t)rd32(e + 8) >> 23) + 0x80, b = (-(int32_t)rd32(e + 0x10) >> 23) + 0x80;
    *x = a < 0 ? 0 : a > 0xFF ? 0xFF : a;
    *y = b < 0 ? 0 : b > 0xFF ? 0xFF : b;
    return 0;
}
static int map_origin(int v) { return v < 0x40 ? 0 : v > 0xC0 ? 0x80 : v - 0x40; }
static void map_hook(RCpu *c) {
    (void)c;
    if ((g_busy && !g_rebuilding) || !p2_active()) return;
    static int on = -1;
    if (on < 0) on = !getenv("AC3_COOP_RADAR") || atoi(getenv("AC3_COOP_RADAR")) != 0;
    uint32_t tab = rd32(A->ent_table), me = rd32(tab), other = rd32(tab + 4);
    int16_t hp;
    memcpy(&hp, psx_ptr(other + 0x4E), 2);
    if (!on || (~rd32(other) & 0x4000) || hp < 1) return;
    int mx, my, ox, oy;
    map_cell(me, &mx, &my);
    map_cell(other, &ox, &oy);
    int px = ox - map_origin(mx), py = oy - map_origin(my);
    if (getenv("AC3_COOP_DBGMAPA"))   /* depuracion: casillas del jugador y del otro, y el punto en la ventana */
        printf("coop: VBlank %d mapa%s: yo %d,%d otro %d,%d punto %d,%d (yo en %d,%d)\n", VSync(-1), g_rebuilding ? " (redibujo)" : "", mx, my, ox,
               oy, px, py, mx - map_origin(mx), my - map_origin(my));
    if (px < 0 || py < 0 || px >= 0x80 || py >= 0x80) return;
    uint32_t counts = rd32(A->hud_counts), base = rd32(A->hud_sprites) + 0x518, n = rd32(counts + 0xC);
    uint8_t tile[0x10];
    memcpy(tile, psx_ptr(base), sizeof tile);   /* plantilla: la primera baldosa (TILE: 3 palabras, codigo 0x60-0x63) */
    if (n > 0x70 || tile[3] != 3 || (tile[7] & 0xFC) != 0x60) return;
    /* un punto azul opaco de 3x3: con la sombra roja del juego (0xFF, 0x40, 0x40, su primera pasada) y el azul
     * semitransparente encima, en la Retroid se veia rojo, como un enemigo (lo vio el dueno) */
    uint8_t *t = psx_ptr(base + n * 0x10);
    memcpy(t + 3, tile + 3, 1);
    t[4] = 0x20; t[5] = 0x60; t[6] = 0xFF;
    t[7] = (uint8_t)(tile[7] & 0xFD);
    int16_t x = (int16_t)(px + 0x60 - 1), y = (int16_t)(py + 0x38 - 1), w = 3;
    memcpy(t + 8, &x, 2); memcpy(t + 10, &y, 2); memcpy(t + 12, &w, 2); memcpy(t + 14, &w, 2);
    wr32(counts + 0xC, n + 1);
}

/* medicion (AC3_COOP_MISIL=1): el blanco con el que se lanza cada misil del jugador (FUN_800672d4, cuarto argumento: el
 * fijado, DAT_800BBA78 + 0x18; -1 sin fijado: el misil va recto) y de quien es */
static void missile_hook(RCpu *c) {
    printf("coop: VBlank %d misil del jugador %d con blanco %d\n", VSync(-1), g_busy ? 2 : 1, (int32_t)c->r[7]);
}

/* (paso 5) la imagen desde el avion del jugador 2, sin tocar la partida. Al empezar el dibujo del paso (entrada de
 * FUN_8007a03c llamada desde FUN_80033910, justo tras la logica: S1) se guardan la RAM, el scratchpad y la GTE. En
 * DrawOTag (la lista maestra de 3 entradas del paso, g_state + 8..0x10) se guarda el estado del final del paso, se vuelve
 * a S1, se quita de la cadena maestra la tabla de orden de la mision (FUN_80033910 la enlazo al empezar: la maestra apunta
 * a su final, su entrada 0 a lo que seguia), se vuelve a llamar a FUN_80033910 con una logica vacia como el jugador 2 (su
 * entidad en la 0, sus bloques, su camara, su blanco: limpia la tabla, la vuelve a enlazar y dibuja el mundo, los
 * objetos y el HUD desde el), se dibuja la lista y todo vuelve al final del paso. Desde S1 y no desde el final: el HUD
 * tiene sublistas fijas que no limpia FUN_80033910 (0x8013b204...); dibujadas dos veces, la cadena se cerraba en un ciclo.
 * Lo que se dibuja fuera de la lista de la mision no sale (la transicion de ruido de las 16 primeras imagenes del vuelo:
 * AC3_COOP_AUTOPRUEBA lo mide) */
static uint8_t g_s1_ram[PSX_RAM_SIZE], g_s1_scr[0x400];
static PsxGte g_s1_gte;
static int g_s1_ok, g_view2 = -1;
static uint32_t g_s1_sp;   /* la pila de FUN_80033910 en S1 */
static int view2_on(void) {
    if (g_view2 < 0) g_view2 = (getenv("AC3_COOP_VER") && atoi(getenv("AC3_COOP_VER")) == 2) || getenv("AC3_COOP_AUTOPRUEBA");
    return g_view2;
}
static void draw_start_hook(RCpu *c) {
    if (g_busy || !view2_on() || !g_draw_sp || c->r[31] < A->mission_draw || c->r[31] >= A->mission_draw + 0x400) return;
    memcpy(g_s1_ram, psx_ram, PSX_RAM_SIZE); memcpy(g_s1_scr, psx_scratch, sizeof g_s1_scr);
    g_s1_gte = *(PsxGte *)psx_gte_get_context();
    g_s1_sp = c->r[29];
    g_s1_ok = 1;
    ac3_rr_mute = 1;   /* (paso 6) el redibujo de las intermedias anota el del jugador 2, no este */
}
static uint8_t g_bars[8];
/* la logica vacia del redibujo: FUN_80033910 pone a cero las bandas de cine (g_render + 0x28C..0x293: altos de los
 * rectangulos negros de arriba y abajo) antes de la logica, que les da valor; aqui vuelven a ser las de S1 */
static void noop_hook(RCpu *c) {
    (void)c;
    if (g_rebuilding) memcpy(psx_ptr(rd32(G_RENDER) + 0x28C), g_bars, sizeof g_bars);
}
static int g_views, g_view_miss;
int ac3_coop_draw_p2(uint32_t ot, void (*draw)(uint32_t ot, void *u), void *u) {
    ac3_rr_mute = 0;
    if (!A || !g_draw_sp || !g_s1_ok || !p2_active()) {
        if (g_draw_sp || g_s1_ok) g_view_miss++;
        g_draw_sp = 0; g_s1_ok = 0;
        return -1;
    }
    static uint8_t ram[PSX_RAM_SIZE], scr[0x400];
    PsxGte *gc = (PsxGte *)psx_gte_get_context(), gte = *gc;
    struct timespec t0, t1, t2, t3;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    memcpy(ram, psx_ram, PSX_RAM_SIZE); memcpy(scr, psx_scratch, sizeof scr);
    memcpy(psx_ram, g_s1_ram, PSX_RAM_SIZE); memcpy(psx_scratch, g_s1_scr, sizeof scr);
    *gc = g_s1_gte;
    g_draw_sp = 0; g_s1_ok = 0;
    uint32_t gr = rd32(G_RENDER), st = rd32(g_state_ptr), otm = rd32(gr + 0x10), otend = (otm + 0x3FFC) & 0xFFFFFF;
    uint32_t p = rd32(st + 8), guard = 0;
    while ((rd32(p | 0x80000000u) & 0xFFFFFF) != otend && (rd32(p | 0x80000000u) & 0xFFFFFF) != 0xFFFFFF && ++guard < 100000)
        p = rd32(p | 0x80000000u) & 0xFFFFFF;
    int r = -1;
    if ((rd32(p | 0x80000000u) & 0xFFFFFF) == otend) {   /* (si no, la tabla de la mision no esta en la cadena) */
        wr32(p | 0x80000000u, (rd32(p | 0x80000000u) & 0xFF000000u) | (rd32(otm) & 0xFFFFFF));
        memcpy(g_bars, psx_ptr(gr + 0x28C), sizeof g_bars);
        RCpu cb;
        memset(&cb, 0, sizeof cb);
        cb.r[29] = g_s1_sp;
        static int self = -1;   /* AC3_COOP_AUTOPRUEBA=1 (prueba): el mismo redibujo, pero como el jugador 1 (igual al original) */
        if (self < 0) self = getenv("AC3_COOP_AUTOPRUEBA") != NULL;
        Swap s;
        if (!self) p2_begin(&s);
        uint32_t tab = rd32(A->ent_table), pe0 = rd32(tab + 4), pe1 = rd32(tab);   /* (tras p2_begin: [0] es la 1) */
        int content = content_mask() & 1;
        if (!self && content) ent_content_swap(tab, pe0, pe1);
        g_rebuilding = 1;
        static uint8_t rt[PSX_RAM_SIZE / 32];
        extern uint8_t *mipsi_rtrace;
        int trace = !self && getenv("AC3_COOP_LECTURAS") && VSync(-1) >= atoi(getenv("AC3_COOP_LECTURAS"));
        if (trace) { memset(rt, 0, sizeof rt); mipsi_rtrace = rt; }
        if (getenv("AC3_COOP_DBGCAM")) {
            uint32_t cam = rd32(rd32(G_RENDER) + 0x28), t0 = rd32(rd32(A->ent_table));
            int16_t a[3], e[3];
            memcpy(a, psx_ptr(cam + 0x14), 6); memcpy(e, psx_ptr(t0 + 0x18), 6);
            printf("coop: VBlank %d redibujo: camara %08x modo %08x ang %d %d %d; tabla[0] %08x ang %d %d %d\n", VSync(-1), cam, rd32(cam), a[0], a[1],
                   a[2], t0, e[0], e[1], e[2]);
        }
        guest_call(&cb, A->mission_draw, A->noop, 0, 0);
        if (trace) {   /* depuracion: lo que el redibujo lee de los bloques del jugador 1 (por su direccion, no por el puntero) */
            mipsi_rtrace = NULL;
            unsetenv("AC3_COOP_LECTURAS");
            for (int i = 0; i < 6; i++) {
                printf("coop: redibujo, lecturas del bloque %d del jugador 1 (%08x):", i, s.keep[i]);
                for (uint32_t o = 0; o < CTX_SIZE[i]; o += 4) {
                    uint32_t a = (s.keep[i] + o) & 0x1FFFFC;
                    if (rt[a >> 5] >> ((a >> 2) & 7) & 1) printf(" +%x", o);
                }
                printf("\n");
            }
            printf("coop: redibujo, lecturas de la entidad del 1 (%08x):", pe1);
            for (uint32_t o = 0; o < ENT_SIZE; o += 4) {
                uint32_t a = (pe1 + o) & 0x1FFFFC;
                if (rt[a >> 5] >> ((a >> 2) & 7) & 1) printf(" +%x", o);
            }
            printf("\n");
        }
        g_rebuilding = 0;
        if (!self && content) ent_content_swap(tab, pe0, pe1);
        if (!self) p2_end(&s);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (getenv("AC3_COOP_TIEMPOS")) {   /* medicion: paquetes de la cadena (y si vuelve sobre si misma) */
            uint32_t a = ot & 0xFFFFFF, n = 0, first_rep = 0;
            static uint8_t seen[PSX_RAM_SIZE / 4];
            memset(seen, 0, sizeof seen);
            while (a != 0xFFFFFF && n < 2000000) {
                if (seen[(a & 0x1FFFFC) >> 2]++ && !first_rep) first_rep = a | 0x80000000u;
                a = rd32(a | 0x80000000u) & 0xFFFFFF; n++;
            }
            printf("coop: cadena de %u paquetes%s (primer repetido %08x)\n", n, n >= 2000000 ? " o mas" : "", first_rep);
        }
        draw(ot, u);
        r = 0;
        g_views++;
    } else clock_gettime(CLOCK_MONOTONIC, &t1);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    memcpy(psx_ram, ram, PSX_RAM_SIZE); memcpy(psx_scratch, scr, sizeof scr);
    *gc = gte;
    clock_gettime(CLOCK_MONOTONIC, &t3);
#define MS(a, b) ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) * 1e-6)
    if (getenv("AC3_COOP_TIEMPOS"))   /* medicion: ms en reconstruir, dibujar y restaurar */
        printf("coop: VBlank %d redibujo %.2f ms, dibujo %.2f ms, restaurar %.2f ms\n", VSync(-1), MS(t0, t1), MS(t1, t2), MS(t2, t3));
    {   /* (paso 6) cada 300 imagenes desde el 2: lo que cuesta (en el aparato, en el registro) */
        static double sum[3], mx;
        static int n;
        double r0 = MS(t0, t1), d0 = MS(t1, t2), x0 = MS(t2, t3);
        sum[0] += r0; sum[1] += d0; sum[2] += x0;
        if (r0 + d0 + x0 > mx) mx = r0 + d0 + x0;
        if (++n == 300) {
            printf("coop: %d imagenes desde el jugador 2 (%d como el original); media: redibujo %.2f ms, dibujo %.2f ms, restaurar %.2f ms; "
                   "maximo %.2f ms\n", g_views, g_view_miss, sum[0] / n, sum[1] / n, sum[2] / n, mx);
            sum[0] = sum[1] = sum[2] = mx = 0; n = 0;
        }
    }
    return r;
}

/* depuracion (AC3_COOP_DBGSONIDO=1): las llamadas a las funciones de sonido mas usadas del juego, con quien llama */
static const uint32_t SND_FN[10] = {0x8002D8E4u, 0x8002D978u, 0x8002DB40u, 0x8002DC60u, 0x8002E3F8u, 0x8002E688u, 0x8002E5F8u,
                                    0x8002E488u, 0x8002E2C0u, 0x8002EE80u};
static void snd_log(const RCpu *c, int k) {
    printf("coop: VBlank %d sonido %08x(%x, %x, %x) desde %08x%s\n", VSync(-1), SND_FN[k], c->r[4], c->r[5], c->r[6], c->r[31],
           g_busy ? " (como el 2)" : "");
}
#define SND_HOOK(k) static void snd_hook##k(RCpu *c) { snd_log(c, k); }
SND_HOOK(0) SND_HOOK(1) SND_HOOK(2) SND_HOOK(3) SND_HOOK(4) SND_HOOK(5) SND_HOOK(6) SND_HOOK(7) SND_HOOK(8) SND_HOOK(9)
static void (*const SND_HOOKS[10])(RCpu *) = {snd_hook0, snd_hook1, snd_hook2, snd_hook3, snd_hook4, snd_hook5, snd_hook6, snd_hook7,
                                              snd_hook8, snd_hook9};

int ac3_coop_install(const Ac3Version *v) {
    A = NULL;
    for (size_t i = 0; i < sizeof ADDR / sizeof ADDR[0]; i++)
        if (!strcmp(ADDR[i].name, v->name)) A = &ADDR[i];
    if (!A) return -1;
    g_gp = v->gp;
    g_state_ptr = v->g_state_ptr;
    mipsi_set_hook(A->after_player, after_player);
    for (int k = 0; k < NORDERS; k++) mipsi_set_hook(A->orders[k], ORDER_HOOKS[k]);
    mipsi_set_hook(A->effects, effects_hook);
    mipsi_set_hook(A->cam_step, camera_hook);
    mipsi_set_hook(A->snd_rel, listener_hook);
    mipsi_set_hook(A->radar_draw, radar_hook);
    mipsi_set_hook(A->map_link, map_hook);
    if (view2_on()) { mipsi_set_hook(A->cam_prep, draw_start_hook); mipsi_set_hook(A->noop, noop_hook); }
    if (getenv("AC3_COOP_MISIL")) mipsi_set_hook(A->missile_launch, missile_hook);
    if (getenv("AC3_COOP_DBGSONIDO")) for (int k = 0; k < 10; k++) mipsi_set_hook(SND_FN[k], SND_HOOKS[k]);
    return 0;
}

int ac3_coop_steps(void) { return g_steps; }
int ac3_coop_views(int *miss) { *miss = g_view_miss; return g_views; }
int ac3_coop_orders(void) { return g_orders; }
int ac3_coop_revives(void) { return g_revives; }
uint32_t ac3_coop_entity(void) { return g_e1; }
