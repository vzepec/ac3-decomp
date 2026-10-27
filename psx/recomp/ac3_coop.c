/* Etapa 9, paso 3: el segundo avion por cambio de contexto (ver ac3_coop.h). */
#include "ac3_coop.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_libetc.h"
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
     {{0x800BE60Cu, 4}, {0x800BB868u, 4}, {0x800C6508u, 0x10}, {0, 0}}},
};

static const CoopAddr *A;
static uint32_t g_gp, g_state_ptr;
static uint32_t g_mine[6], g_input2, g_alloc_end;
/* (paso 4) sus copias del fogonazo de la metralleta (8 bytes) y de la punteria (registros por entidad y
 * globales), en la RAM del juego como sus bloques (la huella y una instantanea las incluyen) */
#define GUNFX_SIZE 8
#define AIM_SIZE (64 * 0x34 + 0x14)
static uint32_t g_gunfx2, g_aim2;
static int g_busy, g_steps, g_orders, g_revives;
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
        if (p > A->heap_limit) { printf("coop: sin memoria en el asignador del juego\n"); return -1; }
        wr32(st + HEAP_PTR, p);
        g_alloc_end = p;
        printf("coop: bloques del jugador 2 en %08x-%08x\n", g_mine[0], p);
    }
    for (int i = 0; i < 6; i++) memcpy(psx_ptr(g_mine[i]), psx_ptr(rd32(A->ctx[i])), CTX_SIZE[i]);
    memcpy(psx_ptr(g_input2), psx_ptr(st + INPUT_OFF), INPUT_SIZE);
    memset(psx_ptr(g_gunfx2), 0, GUNFX_SIZE);
    aim_init();
    memcpy(psx_ptr(e1), psx_ptr(e0), ENT_SIZE);
    wr32(e1, (rd32(e0) & ~0x3Fu) | 1u);   /* ranura 1 */
    static int dx = -1, dz;
    if (dx < 0) {   /* AC3_COOP_POS=dx,dz: donde aparece, en unidades del mundo respecto al jugador 1 */
        dx = 60; dz = 120;
        if (getenv("AC3_COOP_POS")) sscanf(getenv("AC3_COOP_POS"), "%d,%d", &dx, &dz);
    }
    wr32(e1 + 8, rd32(e0 + 8) + ((uint32_t)dx << 12));
    wr32(e1 + 0x10, rd32(e0 + 0x10) + ((uint32_t)dz << 12));
    if (getenv("AC3_COOP_TRAS")) {   /* prueba: detras de la entidad k (misma posicion y rumbo, 150 unidades atras) */
        uint32_t ek = rd32(rd32(A->ent_table) + 4 * (uint32_t)atoi(getenv("AC3_COOP_TRAS")));
        memcpy(psx_ptr(e1 + 8), psx_ptr(ek + 8), 12);
        memcpy(psx_ptr(e1 + 0x18), psx_ptr(ek + 0x18), 6);
        int16_t ry;
        memcpy(&ry, psx_ptr(ek + 0x1A), 2);
        double a = ry * (6.283185307179586 / 16384.0);
        wr32(e1 + 8, rd32(e1 + 8) - (uint32_t)(int32_t)(sin(a) * (150 << 12)));
        wr32(e1 + 0x10, rd32(e1 + 0x10) - (uint32_t)(int32_t)(cos(a) * (150 << 12)));
        memcpy(psx_ptr(e1 + 0x38), psx_ptr(ek + 0x38), 12);   /* su velocidad */
        if (getenv("AC3_COOP_GIRO")) {   /* y girado tantos grados (prueba de la punteria: un misil recto falla) */
            int16_t r = (int16_t)(ry + atoi(getenv("AC3_COOP_GIRO")) * 16384 / 360);
            memcpy(psx_ptr(e1 + 0x1A), &r, 2);
        }
    }
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
typedef struct { uint8_t cam[CAM_SIZE], gr0[4], gr28c[8], v[4][16]; uint32_t camp, gr; } ViewKeep;
static void view_save(ViewKeep *k) {
    k->gr = rd32(G_RENDER); k->camp = rd32(k->gr + 0x28);
    memcpy(k->cam, psx_ptr(k->camp), CAM_SIZE);
    memcpy(k->gr0, psx_ptr(k->gr), 4);
    memcpy(k->gr28c, psx_ptr(k->gr + 0x28C), 8);
    for (int i = 0; i < 4 && A->view[i][1]; i++) memcpy(k->v[i], psx_ptr(A->view[i][0]), A->view[i][1]);
}
static void view_restore(const ViewKeep *k) {
    memcpy(psx_ptr(k->camp), k->cam, CAM_SIZE);
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
    if (keep_view()) view_save(&g_vk);
    swap_in(s);
    g_busy = 1;
}
static void p2_end(const Swap *s) {
    g_busy = 0;
    swap_out(s);
    if (keep_view()) view_restore(&g_vk);
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
        guest_call(c, A->player_step, 0, 0, 0);
        p2_end(&s);
    }
    if (diff) {
        uint32_t lo = (c->r[29] - 0x2000u) & 0x1FFFFFu, hi = 0x200000u;   /* la pila de la llamada */
        int n = 0;
        printf("coop: VBlank %d, el paso del jugador 2 cambia:", VSync(-1));
        for (uint32_t a = 0; a < PSX_RAM_SIZE && n < 24; a += 4) {
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
    return 0;
}

int ac3_coop_steps(void) { return g_steps; }
int ac3_coop_orders(void) { return g_orders; }
int ac3_coop_revives(void) { return g_revives; }
uint32_t ac3_coop_entity(void) { return g_e1; }
