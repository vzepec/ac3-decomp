/* Entorno de ejecucion del codigo recompilado (ver recomp_rt.h). */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "recomp_rt.h"

int64_t rc_budget = INT64_MAX;
uint32_t rc_ret;
PsxGte *rc_gte;

/* tabla de funciones traducidas (la registra el anfitrion: rc_register) */
static const RcFunc *g_funcs;
static unsigned g_nfuncs;
static void (*g_libcall)(RCpu *, uint32_t);
static uint32_t (*g_io_read)(uint32_t, int);
static void (*g_io_write)(uint32_t, int, uint32_t);

/* fallos: dentro de rc_run, longjmp; si no, se registran y se sigue */
static jmp_buf *g_jmp;
static int g_fault;
static uint32_t g_fault_pc, g_fault_addr;

void rc_register(const RcFunc *funcs, unsigned n) { g_funcs = funcs; g_nfuncs = n; }

/* overlays: todos se cargan en 0x800C9DC0; el anfitrion dice cual esta cargado */
static const RcFunc *g_ovl[8];
static unsigned g_novl[8];
static int g_cur_ovl;
void rc_register_overlay(int n, const RcFunc *funcs, unsigned count) { if (n > 0 && n < 8) { g_ovl[n] = funcs; g_novl[n] = count; } }
void rc_set_overlay(int n) { g_cur_ovl = n; }
void rc_set_libcall(void (*fn)(RCpu *, uint32_t)) { g_libcall = fn; }
void rc_set_io(uint32_t (*rd)(uint32_t, int), void (*wr)(uint32_t, int, uint32_t)) { g_io_read = rd; g_io_write = wr; }

void rc_fault(int code, uint32_t pc, uint32_t addr) {
    g_fault = code; g_fault_pc = pc; g_fault_addr = addr;
    if (g_jmp) longjmp(*g_jmp, code);
    fprintf(stderr, "recomp: fallo %d en %08x (direccion %08x)\n", code, pc, addr);
}

uint32_t rc_io_read(uint32_t a, int size, uint32_t pc) {
    if (g_io_read) return g_io_read(a, size);
    rc_fault(RC_IO, pc, a);
    return 0;
}
void rc_io_write(uint32_t a, int size, uint32_t v, uint32_t pc) {
    if (g_io_write) { g_io_write(a, size, v); return; }
    rc_fault(RC_IO, pc, a);
}

static void (*find(const RcFunc *t, unsigned n, uint32_t addr))(RCpu *) {
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        unsigned m = (lo + hi) / 2;
        if (t[m].addr == addr) return t[m].fn;
        if (t[m].addr < addr) lo = m + 1; else hi = m;
    }
    return 0;
}

/* firmas de los overlays: si estan registradas, el overlay cargado se detecta solo */
static const uint8_t (*g_sigs)[64];
static int g_nsigs;
void rc_set_overlay_sigs(const uint8_t (*sigs)[64], int n) { g_sigs = sigs; g_nsigs = n; }

static void (*lookup(uint32_t addr))(RCpu *) {
    void (*fn)(RCpu *) = find(g_funcs, g_nfuncs, addr);
    if (!fn && addr >= 0x800C9DC0u) {
        if (g_sigs) {
            const uint8_t *ram = psx_ram + 0xC9DC0;
            g_cur_ovl = 0;
            for (int k = 0; k < g_nsigs; k++)
                if (!memcmp(ram, g_sigs[k], 64)) { g_cur_ovl = k + 1; break; }
        }
        if (g_cur_ovl) fn = find(g_ovl[g_cur_ovl], g_novl[g_cur_ovl], addr);
    }
    return fn;
}


void rc_call(RCpu *c, uint32_t addr) {
    void (*fn)(RCpu *) = lookup(addr);
    if (fn) { fn(c); c->r[0] = 0; return; }
    if (g_libcall && ((addr >= 0x80099DE0u && addr < 0x800AE09Cu) || addr == 0x80025214u)) { g_libcall(c, addr); return; }
    rc_fault(RC_NOFUNC, 0, addr);
}

static const RcFunc *g_hle;
static unsigned g_nhle;
void rc_set_hle(const RcFunc *table, unsigned n) { g_hle = table; g_nhle = n; }

void rc_libcall(RCpu *c, uint32_t addr) {
    if (g_libcall) { g_libcall(c, addr); c->r[0] = 0; return; }
    void (*fn)(RCpu *) = g_hle ? find(g_hle, g_nhle, addr) : 0;
    if (fn) { fn(c); c->r[0] = 0; return; }
    rc_fault(RC_NOFUNC, 0, addr);
}

/* ---- reloj del modo nativo ---- */
static void (*g_tick)(void);
static int64_t g_tick_every;
static int g_in_tick;
void rc_set_tick(void (*fn)(void), int64_t every) { g_tick = fn; g_tick_every = every; rc_budget = every; }
static void (*g_frame_hook)(void);
void rc_set_frame_hook(void (*fn)(void)) { g_frame_hook = fn; }
void rc_vsync_hook(void) { if (g_tick) rc_budget = g_tick_every; if (g_frame_hook) g_frame_hook(); }

void rc_budget_out(uint32_t pc) {
    if (g_jmp || !g_tick) { rc_fault(RC_TIMEOUT, pc, 0); rc_budget = INT64_MAX; return; }
    rc_budget = g_tick_every;
    if (g_in_tick) return;
    g_in_tick = 1;
    g_tick();
    g_in_tick = 0;
}

/* callbacks de la capa (VSync, CD, MDEC, eventos): como una interrupcion, con su propio
 * estado de CPU y una pila en la RAM baja (0x80000000-0x8000FFFF, de la BIOS: el juego no la
 * usa). Se conserva la direccion de retorno esperada del codigo interrumpido. */
void rc_host_callback(uint32_t addr, uint32_t a0, uint32_t a1) {
    static int depth;
    RCpu cb;
    memset(&cb, 0, sizeof cb);
    cb.r[4] = a0; cb.r[5] = a1;
    cb.r[28] = 0x800BA9F4u;
    cb.r[29] = 0x8000F000u - 0x800u * (uint32_t)depth;
    cb.r[31] = 0x8000FFF0u;
    uint32_t ret = rc_ret;
    depth++;
    rc_ret = cb.r[31];
    rc_call(&cb, addr);
    depth--;
    rc_ret = ret;
}

/* ejecuta la funcion `addr` con el estado c; devuelve RC_OK o el codigo de fallo */
int rc_run(RCpu *c, uint32_t addr, int64_t budget, uint32_t *fault_pc, uint32_t *fault_addr) {
    jmp_buf jb, *prev = g_jmp;
    g_jmp = &jb;
    g_fault = 0;
    rc_budget = budget;
    if (!rc_gte) rc_gte = psx_gte_get_context();
    int r = setjmp(jb);
    if (r == 0) { rc_ret = c->r[31]; rc_call(c, addr); }
    g_jmp = prev;
    if (fault_pc) *fault_pc = g_fault_pc;
    if (fault_addr) *fault_addr = g_fault_addr;
    return r ? g_fault : RC_OK;
}
