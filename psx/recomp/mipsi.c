/* Interprete MIPS R3000 del codigo del juego (etapa 7, paso 1). Codigo nuestro: ejecuta en el
 * aparato el codigo que hay en la RAM (ACE.BIN y overlays, leidos del disco del usuario), en vez
 * del codigo traducido a C que no se puede distribuir (docs/12-etapa7.md).
 *
 * Misma semantica que el codigo traducido (tools/recomp/recomp.py) y que el interprete de
 * referencia (tools/mipsemu.py):
 *  - memoria con rc_lw/rc_sw... de recomp_rt.h (mismos fallos);
 *  - hueco de retardo: la condicion del salto (y el registro de jr/jalr) se lee antes de la
 *    instruccion del hueco; jal/jalr/bal escriben el enlace antes del hueco;
 *  - las funciones de libreria de Sony (rango de la libreria y __main) las atiende rc_libcall,
 *    y luego se sigue en $ra (como al volver de ellas);
 *  - cop0, break, syscall e instrucciones desconocidas: rc_illegal / rc_break / rc_syscall.
 * Bucle plano: se interpreta desde `addr` hasta que el pc llega a la direccion de retorno que
 * espera quien llamo (rc_ret), como el interprete de referencia con RETURN_MAGIC. Las llamadas
 * internas no recursan en C; las retrollamadas de la capa (VSync, CD...) si entran de nuevo.
 * Presupuesto del reloj (RC_BUDGET) en los mismos sitios que el codigo traducido: al entrar en
 * una funcion (jal/jalr) y en los saltos hacia atras. Modo mixto: si hay una funcion traducida
 * registrada para el destino de un jal/jalr, se llama a ella (rc_find). */
#include <string.h>

#include "recomp_rt.h"

int rc_is_lib(uint32_t a);                      /* recomp_rt.c */
void (*rc_find(uint32_t addr))(RCpu *);         /* recomp_rt.c: funcion traducida o NULL */

/* instrucciones ejecutadas (medicion) */
uint64_t mipsi_steps;

/* enganche de pruebas: se llama al entrar en la funcion `addr` (p. ej. forzar la mision que
 * carga el juego: AC3_MISSION en native_main) */
static uint32_t g_hook_addr;
static void (*g_hook)(RCpu *);
void mipsi_set_hook(uint32_t addr, void (*fn)(RCpu *)) { g_hook_addr = addr; g_hook = fn; }

static inline uint32_t fetch(uint32_t pc) {
    uint8_t *p = rc_ptr(pc, pc);
    if (!p || (pc & 3)) { rc_fault(p ? RC_FAULT_ALIGN : RC_FAULT_MEM, pc, pc); return 0; }
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* instruccion que no es salto (la del hueco de retardo incluida) */
static inline void exec(RCpu *c, uint32_t w, uint32_t pc) {
    uint32_t *r = c->r;
    uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31;
    uint32_t sh = (w >> 6) & 31, fn = w & 63, imm = w & 0xFFFF;
    uint32_t simm = (uint32_t)(int32_t)(int16_t)imm;
    if (w == 0) return;
    switch (op) {
    case 0x00:
        switch (fn) {
        case 0x00: r[rd] = r[rt] << sh; break;
        case 0x02: r[rd] = r[rt] >> sh; break;
        case 0x03: r[rd] = (uint32_t)((int32_t)r[rt] >> sh); break;
        case 0x04: r[rd] = r[rt] << (r[rs] & 31); break;
        case 0x06: r[rd] = r[rt] >> (r[rs] & 31); break;
        case 0x07: r[rd] = (uint32_t)((int32_t)r[rt] >> (r[rs] & 31)); break;
        case 0x0C: rc_syscall(c, pc); break;
        case 0x0D: rc_break(c, pc); break;
        case 0x10: r[rd] = c->hi; break;
        case 0x11: c->hi = r[rs]; break;
        case 0x12: r[rd] = c->lo; break;
        case 0x13: c->lo = r[rs]; break;
        case 0x18: rc_mult(c, r[rs], r[rt]); break;
        case 0x19: rc_multu(c, r[rs], r[rt]); break;
        case 0x1A: rc_div(c, r[rs], r[rt]); break;
        case 0x1B: rc_divu(c, r[rs], r[rt]); break;
        case 0x20: case 0x21: r[rd] = r[rs] + r[rt]; break;
        case 0x22: case 0x23: r[rd] = r[rs] - r[rt]; break;
        case 0x24: r[rd] = r[rs] & r[rt]; break;
        case 0x25: r[rd] = r[rs] | r[rt]; break;
        case 0x26: r[rd] = r[rs] ^ r[rt]; break;
        case 0x27: r[rd] = ~(r[rs] | r[rt]); break;
        case 0x2A: r[rd] = (int32_t)r[rs] < (int32_t)r[rt]; break;
        case 0x2B: r[rd] = r[rs] < r[rt]; break;
        default: rc_illegal(c, pc); break;
        }
        break;
    case 0x08: case 0x09: r[rt] = r[rs] + simm; break;
    case 0x0A: r[rt] = (int32_t)r[rs] < (int32_t)simm; break;
    case 0x0B: r[rt] = r[rs] < simm; break;
    case 0x0C: r[rt] = r[rs] & imm; break;
    case 0x0D: r[rt] = r[rs] | imm; break;
    case 0x0E: r[rt] = r[rs] ^ imm; break;
    case 0x0F: r[rt] = imm << 16; break;
    case 0x12:
        if (w & 0x02000000) rc_gte_cmd(w & 0x1FFFFFF);
        else if (rs == 0) r[rt] = rc_mfc2((int)rd);
        else if (rs == 2) r[rt] = rc_cfc2((int)rd);
        else if (rs == 4) rc_mtc2((int)rd, r[rt]);
        else if (rs == 6) rc_ctc2((int)rd, r[rt]);
        else rc_illegal(c, pc);
        break;
    case 0x20: r[rt] = (uint32_t)(int32_t)(int8_t)rc_lb(r[rs] + simm, pc); break;
    case 0x21: r[rt] = (uint32_t)(int32_t)(int16_t)rc_lh(r[rs] + simm, pc); break;
    case 0x22: r[rt] = rc_lwl(r[rt], r[rs] + simm, pc); break;
    case 0x23: r[rt] = rc_lw(r[rs] + simm, pc); break;
    case 0x24: r[rt] = rc_lb(r[rs] + simm, pc); break;
    case 0x25: r[rt] = rc_lh(r[rs] + simm, pc); break;
    case 0x26: r[rt] = rc_lwr(r[rt], r[rs] + simm, pc); break;
    case 0x28: rc_sb(r[rs] + simm, r[rt], pc); break;
    case 0x29: rc_sh(r[rs] + simm, r[rt], pc); break;
    case 0x2A: rc_swl(r[rs] + simm, r[rt], pc); break;
    case 0x2B: rc_sw(r[rs] + simm, r[rt], pc); break;
    case 0x2E: rc_swr(r[rs] + simm, r[rt], pc); break;
    case 0x32: rc_mtc2((int)rt, rc_lw(r[rs] + simm, pc)); break;
    case 0x3A: rc_sw(r[rs] + simm, rc_mfc2((int)rt), pc); break;
    default: rc_illegal(c, pc); break;   /* cop0 (0x10) incluido, como el codigo traducido */
    }
    r[0] = 0;
}

/* llamada a `tgt` desde el pc actual con el enlace ya escrito en $ra: funcion de libreria,
 * funcion traducida (modo mixto) o se sigue interpretando alli; devuelve el pc siguiente */
static inline uint32_t call_to(RCpu *c, uint32_t tgt, uint32_t link) {
    if (rc_is_lib(tgt)) { rc_libcall(c, tgt); return link; }
    void (*fn)(RCpu *) = rc_find(tgt);
    if (fn) { rc_ret = link; fn(c); c->r[0] = 0; return link; }
    if (g_hook && tgt == g_hook_addr) g_hook(c);
    RC_BUDGET(tgt);
    return tgt;
}

void mipsi_call(RCpu *c, uint32_t addr) {
    uint32_t ret = rc_ret;   /* direccion de retorno que espera quien llamo */
    uint32_t pc = addr;
    /* entrar en la funcion: como un salto a ella (libreria, traducida o interpretada) */
    if (rc_is_lib(pc)) { rc_libcall(c, pc); return; }
    if (g_hook && pc == g_hook_addr) g_hook(c);
    RC_BUDGET(pc);
    while (pc != ret) {
        uint32_t w = fetch(pc);
        uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31;
        mipsi_steps++;
        if (op >= 2 && op <= 7) {   /* j, jal, beq, bne, blez, bgtz */
            uint32_t tgt;
            int take;
            if (op <= 3) { tgt = ((pc + 4) & 0xF0000000u) | ((w & 0x3FFFFFFu) << 2); take = 1; }
            else {
                tgt = pc + 4 + ((uint32_t)(int32_t)(int16_t)(w & 0xFFFF) << 2);
                int32_t a = (int32_t)c->r[rs], b = (int32_t)c->r[rt];
                take = op == 4 ? a == b : op == 5 ? a != b : op == 6 ? a <= 0 : a > 0;
            }
            if (op == 3) c->r[31] = pc + 8;
            mipsi_steps++;
            exec(c, fetch(pc + 4), pc + 4);
            if (op == 3) { pc = call_to(c, tgt, pc + 8); continue; }
            if (!take) { pc += 8; continue; }
            if (op == 2 && rc_is_lib(tgt)) { rc_libcall(c, tgt); pc = c->r[31]; continue; }   /* salto de cola */
            if (tgt <= pc) RC_BUDGET(pc);
            pc = tgt;
            continue;
        }
        if (op == 1) {   /* bltz, bgez, bltzal, bgezal */
            uint32_t tgt = pc + 4 + ((uint32_t)(int32_t)(int16_t)(w & 0xFFFF) << 2);
            int32_t a = (int32_t)c->r[rs];
            int take = (rt & 1) ? a >= 0 : a < 0;
            if (rt & 0x10) c->r[31] = pc + 8;
            mipsi_steps++;
            exec(c, fetch(pc + 4), pc + 4);
            if (!take) { pc += 8; continue; }
            if (rt & 0x10) { pc = call_to(c, tgt, pc + 8); continue; }
            if (tgt <= pc) RC_BUDGET(pc);
            pc = tgt;
            continue;
        }
        if (op == 0 && ((w & 63) == 8 || (w & 63) == 9)) {   /* jr, jalr */
            uint32_t tgt = c->r[rs];
            uint32_t rd = (w >> 11) & 31;
            int link = (w & 63) == 9;
            if (link && rd) c->r[rd] = pc + 8;
            mipsi_steps++;
            exec(c, fetch(pc + 4), pc + 4);
            if (link) { pc = call_to(c, tgt, pc + 8); continue; }
            if (rs != 31) {
                if (rc_is_lib(tgt)) { rc_libcall(c, tgt); pc = c->r[31]; continue; }
                RC_BUDGET(pc);   /* switch por tabla */
            }
            pc = tgt;
            continue;
        }
        exec(c, w, pc);
        pc += 4;
    }
}
