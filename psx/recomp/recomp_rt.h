/* Entorno de ejecucion del codigo recompilado (etapa 6, paso 3). Codigo nuestro: el C que
 * genera tools/recomp/recomp.py (en data/recomp/, fuera de git) solo incluye esta cabecera.
 * Semantica identica a la del interprete de referencia (tools/mipsemu.py):
 *  - memoria: RAM de 2 MB en KUSEG/KSEG0/KSEG1 (espejos hasta 8 MB), scratchpad en
 *    0x1F800000, registros de E/S en 0x1F801000-0x1F802FFF (rc_io); el resto falla;
 *  - lw/sw desalineados fallan (lh/sh no se comprueban, como en el interprete);
 *  - div/divu con divisor 0 dan el resultado del R3000;
 *  - break/syscall/instrucciones ilegales llaman a rc_fault.
 * Fallos y presupuesto de instrucciones: en las pruebas, rc_fault salta (longjmp) a quien
 * llamo a rc_run; en el juego, se registran. */
#ifndef RECOMP_RT_H
#define RECOMP_RT_H

#include <stdint.h>

#include "psx_gte.h"
#include "psx_mem.h"

typedef struct { uint32_t r[32]; uint32_t hi, lo; } RCpu;
typedef struct { uint32_t addr; void (*fn)(RCpu *); } RcFunc;

enum { RC_OK = 0, RC_FAULT_MEM = 1, RC_FAULT_ALIGN = 2, RC_BREAK = 3, RC_SYSCALL = 4, RC_ILLEGAL = 5,
       RC_TIMEOUT = 6, RC_NOFUNC = 7, RC_IO = 8, RC_BADRA = 9 };

extern int64_t rc_budget;
/* direccion de retorno esperada por la siguiente funcion llamada (ver recomp.py) */
extern uint32_t rc_ret;
extern PsxGte *rc_gte;
void rc_fault(int code, uint32_t pc, uint32_t addr);

/* ---- memoria ---- */
uint32_t rc_io_read(uint32_t a, int size, uint32_t pc);
void rc_io_write(uint32_t a, int size, uint32_t v, uint32_t pc);

static inline uint8_t *rc_ptr(uint32_t a, uint32_t pc) {
    uint32_t seg = a & 0xE0000000u;
    if ((seg == 0 || seg == 0x80000000u || seg == 0xA0000000u) && (a & 0x1FFFFFFFu) < 0x800000u)
        return psx_ram + (a & (PSX_RAM_SIZE - 1));
    if ((a & 0xFFFFFC00u) == PSX_SCRATCH_ADDR) return psx_scratch + (a & 0x3FF);
    return 0;
}
static inline int rc_isio(uint32_t a) { return a >= 0x1F801000u && a < 0x1F803000u; }

static inline uint32_t rc_lw(uint32_t a, uint32_t pc) {
    if (a & 3) rc_fault(RC_FAULT_ALIGN, pc, a);
    uint8_t *p = rc_ptr(a, pc);
    if (!p) { if (rc_isio(a)) return rc_io_read(a, 4, pc); rc_fault(RC_FAULT_MEM, pc, a); }
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint32_t rc_lh(uint32_t a, uint32_t pc) {
    uint8_t *p = rc_ptr(a, pc);
    if (!p) { if (rc_isio(a)) return rc_io_read(a, 2, pc) & 0xFFFF; rc_fault(RC_FAULT_MEM, pc, a); }
    return (uint32_t)(p[0] | p[1] << 8);
}
static inline uint32_t rc_lb(uint32_t a, uint32_t pc) {
    uint8_t *p = rc_ptr(a, pc);
    if (!p) { if (rc_isio(a)) return rc_io_read(a, 1, pc) & 0xFF; rc_fault(RC_FAULT_MEM, pc, a); }
    return p[0];
}
static inline void rc_sw(uint32_t a, uint32_t v, uint32_t pc) {
    if (a & 3) rc_fault(RC_FAULT_ALIGN, pc, a);
    uint8_t *p = rc_ptr(a, pc);
    if (!p) { if (rc_isio(a)) { rc_io_write(a, 4, v, pc); return; } rc_fault(RC_FAULT_MEM, pc, a); }
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void rc_sh(uint32_t a, uint32_t v, uint32_t pc) {
    uint8_t *p = rc_ptr(a, pc);
    if (!p) { if (rc_isio(a)) { rc_io_write(a, 2, v & 0xFFFF, pc); return; } rc_fault(RC_FAULT_MEM, pc, a); }
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static inline void rc_sb(uint32_t a, uint32_t v, uint32_t pc) {
    uint8_t *p = rc_ptr(a, pc);
    if (!p) { if (rc_isio(a)) { rc_io_write(a, 1, v & 0xFF, pc); return; } rc_fault(RC_FAULT_MEM, pc, a); }
    p[0] = (uint8_t)v;
}
/* lwl/lwr/swl/swr en little-endian (mismas formulas que tools/mipsemu.py) */
static inline uint32_t rc_lwl(uint32_t rt, uint32_t a, uint32_t pc) {
    uint32_t w = rc_lw(a & ~3u, pc), k = a & 3;
    return (rt & (0x00FFFFFFu >> (8 * k))) | (w << (8 * (3 - k)));
}
static inline uint32_t rc_lwr(uint32_t rt, uint32_t a, uint32_t pc) {
    uint32_t w = rc_lw(a & ~3u, pc), k = a & 3;
    return (rt & (uint32_t)((uint64_t)0xFFFFFF00u << (8 * (3 - k)))) | (w >> (8 * k));
}
static inline void rc_swl(uint32_t a, uint32_t rt, uint32_t pc) {
    uint32_t w = rc_lw(a & ~3u, pc), k = a & 3;
    w = (w & (uint32_t)((uint64_t)0xFFFFFF00u << (8 * k))) | (rt >> (8 * (3 - k)));
    rc_sw(a & ~3u, w, pc);
}
static inline void rc_swr(uint32_t a, uint32_t rt, uint32_t pc) {
    uint32_t w = rc_lw(a & ~3u, pc), k = a & 3;
    w = (w & (0x00FFFFFFu >> (8 * (3 - k)))) | (rt << (8 * k));
    rc_sw(a & ~3u, w, pc);
}

/* ---- aritmetica ---- */
static inline void rc_mult(RCpu *c, uint32_t a, uint32_t b) {
    int64_t p = (int64_t)(int32_t)a * (int32_t)b;
    c->lo = (uint32_t)p; c->hi = (uint32_t)((uint64_t)p >> 32);
}
static inline void rc_multu(RCpu *c, uint32_t a, uint32_t b) {
    uint64_t p = (uint64_t)a * b;
    c->lo = (uint32_t)p; c->hi = (uint32_t)(p >> 32);
}
static inline void rc_div(RCpu *c, uint32_t ua, uint32_t ub) {
    int32_t a = (int32_t)ua, b = (int32_t)ub;
    if (b == 0) { c->lo = a < 0 ? 1 : 0xFFFFFFFFu; c->hi = ua; }
    else if (a == INT32_MIN && b == -1) { c->lo = 0x80000000u; c->hi = 0; }
    else { c->lo = (uint32_t)(a / b); c->hi = (uint32_t)(a % b); }
}
static inline void rc_divu(RCpu *c, uint32_t a, uint32_t b) {
    if (b == 0) { c->lo = 0xFFFFFFFFu; c->hi = a; }
    else { c->lo = a / b; c->hi = a % b; }
}

/* ---- GTE (la de psx/) ---- */
static inline void rc_gte_cmd(uint32_t cmd) { psx_gte_command(rc_gte, cmd); }
static inline uint32_t rc_mfc2(int r) { return psx_gte_read_data(rc_gte, r); }
static inline uint32_t rc_cfc2(int r) { return psx_gte_read_ctrl(rc_gte, r); }
static inline void rc_mtc2(int r, uint32_t v) { psx_gte_write_data(rc_gte, r, v); }
static inline void rc_ctc2(int r, uint32_t v) { psx_gte_write_ctrl(rc_gte, r, v); }

/* ---- control ---- */
/* presupuesto: en las pruebas, agotarlo es tiempo agotado; en el juego nativo, es el reloj
 * (rc_budget_out llama al VBlank de la capa y lo recarga) */
void rc_budget_out(uint32_t pc);
#define RC_BUDGET(pc) do { if (--rc_budget < 0) rc_budget_out(pc); } while (0)
static inline void rc_break(RCpu *c, uint32_t pc) { (void)c; rc_fault(RC_BREAK, pc, 0); }
static inline void rc_syscall(RCpu *c, uint32_t pc) { (void)c; rc_fault(RC_SYSCALL, pc, 0); }
static inline void rc_illegal(RCpu *c, uint32_t pc) { (void)c; rc_fault(RC_ILLEGAL, pc, 0); }
/* llamada por direccion (jalr, punteros a funcion, saltos de cola fuera de la funcion) */
void rc_call(RCpu *c, uint32_t addr);
/* llamada a una funcion de libreria de Sony: la atiende el anfitrion (psx/ o, en las
 * pruebas, un stub determinista) */
void rc_libcall(RCpu *c, uint32_t addr);

/* ---- modo nativo (psx/recomp/native_main.c y la app de Android) ---- */
void rc_set_hle(const RcFunc *table, unsigned n);              /* enlace generado (gen_hle.py) */
void rc_set_overlay_sigs(const uint8_t (*sigs)[64], int n);    /* deteccion del overlay cargado */
void rc_set_tick(void (*fn)(void), int64_t every);             /* reloj: VBlank cada `every` */
void rc_vsync_hook(void);                                      /* el juego llamo a VSync */
void rc_set_frame_hook(void (*fn)(void));                       /* aviso al host tras VSync */
void rc_host_callback(uint32_t addr, uint32_t a0, uint32_t a1);  /* callbacks de la capa */

#endif
