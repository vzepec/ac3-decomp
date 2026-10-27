/* Etapa 9, paso 3: el segundo avion por cambio de contexto (ver ac3_coop.h). */
#include "ac3_coop.h"

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
} CoopAddr;

static const uint32_t CTX_SIZE[6] = {8, 0x10, 0x10, 0x70, 0x3C, 0x38};
#define ENT_SIZE 0x58      /* la entidad del jugador (las de los cazas, 0x5C) */
#define INPUT_OFF 0x120    /* entrada del juego en g_state */
#define INPUT_SIZE 0x40
#define PAD_OUT 0x50       /* salida de la ranura de mando 0 en g_state; 0x1A por ranura */
#define PAD_CFG 0x15C      /* configuracion de botones en g_state */
#define HEAP_PTR 0x1C      /* puntero del asignador en g_state */

static const CoopAddr ADDR[] = {
    {"US", 0x800BB824u, {0x800BBA68u, 0x800BBA6Cu, 0x800BBA70u, 0x800BBA74u, 0x800BBA78u, 0x800BBA80u},
     0x80044F70u, 0x800715FCu, 0x800288D8u, 0x801FB700u},
};

static const CoopAddr *A;
static uint32_t g_gp, g_state_ptr;
static uint32_t g_mine[6], g_input2, g_alloc_end;
static int g_busy, g_steps;
static uint32_t g_e1;

static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, psx_ptr(a), 4); return v; }
static void wr32(uint32_t a, uint32_t v) { memcpy(psx_ptr(a), &v, 4); }

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
        if (p > A->heap_limit) { printf("coop: sin memoria en el asignador del juego\n"); return -1; }
        wr32(st + HEAP_PTR, p);
        g_alloc_end = p;
        printf("coop: bloques del jugador 2 en %08x-%08x\n", g_mine[0], p);
    }
    for (int i = 0; i < 6; i++) memcpy(psx_ptr(g_mine[i]), psx_ptr(rd32(A->ctx[i])), CTX_SIZE[i]);
    memcpy(psx_ptr(g_input2), psx_ptr(st + INPUT_OFF), INPUT_SIZE);
    memcpy(psx_ptr(e1), psx_ptr(e0), ENT_SIZE);
    wr32(e1, (rd32(e0) & ~0x3Fu) | 1u);   /* ranura 1 */
    static int dx = -1, dz;
    if (dx < 0) {   /* AC3_COOP_POS=dx,dz: donde aparece, en unidades del mundo respecto al jugador 1 */
        dx = 60; dz = 120;
        if (getenv("AC3_COOP_POS")) sscanf(getenv("AC3_COOP_POS"), "%d,%d", &dx, &dz);
    }
    wr32(e1 + 8, rd32(e0 + 8) + ((uint32_t)dx << 12));
    wr32(e1 + 0x10, rd32(e0 + 0x10) + ((uint32_t)dz << 12));
    printf("coop: entidad 1 (%08x) activa en el VBlank %d\n", e1, VSync(-1));
    return 0;
}

static void after_player(RCpu *c) {
    if (g_busy) return;
    uint32_t st = rd32(g_state_ptr), tab = rd32(A->ent_table);
    uint32_t e0 = rd32(tab), e1 = rd32(tab + 4);
    if (!(rd32(e1) & ~0x3Fu) && coop_init(st, e0, e1)) return;
    g_e1 = e1;
    /* el mando del puerto 2 (ranura 4) en la entrada del jugador 2 */
    guest_call(c, A->pad_map, st + PAD_OUT + 4 * 0x1A, g_input2, st + PAD_CFG);
    /* el modo del jugador (A + 0, A + 4: piloto automatico del inicio, control...) lo cambian ordenes de la
     * mision que solo llegan al bloque del jugador 1 (FUN_80074e9c, FUN_80074ef8...): mientras el jugador 2
     * viva, el suyo sigue al del 1. SIN RESOLVER: ejecutar esas ordenes tambien para el jugador 2 */
    int16_t hp2;
    memcpy(&hp2, psx_ptr(e1 + 0x4E), 2);
    if (hp2 > 0) { wr32(g_mine[3], rd32(rd32(A->ctx[3]))); wr32(g_mine[3] + 4, rd32(rd32(A->ctx[3]) + 4)); }
    /* cambio de contexto */
    uint32_t keep[6];
    uint8_t in1[INPUT_SIZE];
    for (int i = 0; i < 6; i++) { keep[i] = rd32(A->ctx[i]); wr32(A->ctx[i], g_mine[i]); }
    memcpy(in1, psx_ptr(st + INPUT_OFF), INPUT_SIZE);
    memcpy(psx_ptr(st + INPUT_OFF), psx_ptr(g_input2), INPUT_SIZE);
    wr32(tab, e1);
    wr32(tab + 4, e0);
    g_busy = 1;
    guest_call(c, A->player_step, 0, 0, 0);
    g_busy = 0;
    /* todo vuelve a ser del jugador 1 */
    wr32(tab, e0);
    wr32(tab + 4, e1);
    memcpy(psx_ptr(g_input2), psx_ptr(st + INPUT_OFF), INPUT_SIZE);
    memcpy(psx_ptr(st + INPUT_OFF), in1, INPUT_SIZE);
    for (int i = 0; i < 6; i++) wr32(A->ctx[i], keep[i]);
    g_steps++;
    static int log_every = -1;   /* AC3_COOP_LOG=n: cada n pasos, los dos aviones */
    if (log_every < 0) log_every = getenv("AC3_COOP_LOG") ? atoi(getenv("AC3_COOP_LOG")) : 0;
    if (log_every > 0 && g_steps % log_every == 0)
        for (int k = 0; k < 2; k++) {
            uint32_t e = k ? e1 : e0;
            int16_t ang[3], hp;
            memcpy(ang, psx_ptr(e + 0x18), 6);
            memcpy(&hp, psx_ptr(e + 0x4E), 2);
            printf("coop: VBlank %d jugador %d pos %d %d %d ang %d %d %d vel %d %d %d vida %d banderas %08x\n", VSync(-1), k + 1,
                   (int32_t)rd32(e + 8) >> 12, (int32_t)rd32(e + 0xC) >> 12, (int32_t)rd32(e + 0x10) >> 12, ang[0], ang[1], ang[2],
                   (int32_t)rd32(e + 0x38), (int32_t)rd32(e + 0x3C), (int32_t)rd32(e + 0x40), hp, rd32(e));
        }
}

int ac3_coop_install(const Ac3Version *v) {
    A = NULL;
    for (size_t i = 0; i < sizeof ADDR / sizeof ADDR[0]; i++)
        if (!strcmp(ADDR[i].name, v->name)) A = &ADDR[i];
    if (!A) return -1;
    g_gp = v->gp;
    g_state_ptr = v->g_state_ptr;
    mipsi_set_hook(A->after_player, after_player);
    return 0;
}

int ac3_coop_steps(void) { return g_steps; }
uint32_t ac3_coop_entity(void) { return g_e1; }
