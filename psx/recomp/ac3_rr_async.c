/* Etapa 8: el redibujo del mundo estático en un hilo de dibujo (ver ac3_rr_async.h).
 *
 * Redibujar en el hilo del juego le añade el dibujo estático entero por cada intermedia (en
 * ciudades, de 20 a 30 fps en la Retroid con la 1.9.5). Para hacerlo en otro hilo hace falta un
 * intérprete que no comparta estado con el del juego: mipsi.c, recomp_rt.c, mem.c (la RAM), gte.c
 * y libgte.c (la GTE y su contexto), libgpu.c, hle_eu.c, hle_us.c, ac3_versions.c y ac3_rerender.c
 * se enlazan dos veces, la segunda con todos sus símbolos con el prefijo rrw_ (llvm-objcopy
 * --redefine-syms, como el código americano en android_game/build.sh). Esa copia tiene su RAM, su
 * scratchpad, su GTE, sus enganches y su presupuesto. Se comparten solo las tablas matemáticas de
 * libgte_math.c (de solo lectura tras iniciarlas) y funciones sin estado. El hilo del juego solo
 * deja una instantánea (ac3_rr_snapshot): S0 queda en un búfer reservado hasta que el hilo de
 * dibujo termina (ac3_rr_release). */
#include "ac3_rr_async.h"

#include <stdint.h>
#include <string.h>

#include "ac3_rerender.h"
#include "psx_mem.h"

/* la segunda copia */
extern uint8_t rrw_psx_ram[], rrw_psx_scratch[];
extern const Ac3Version rrw_ac3_versions[];
int rrw_ac3_rr_install(const Ac3Version *v);
int rrw_ac3_rr_build_from(const RrSnap *s, uint32_t ot, float t);
void rrw_ac3_wide_install(const Ac3Version *v);
void rrw_rc_set_region(uint32_t ovl_base, uint32_t lib_lo, uint32_t lib_hi, uint32_t lib_main, uint32_t gp);
void rrw_rc_set_interpreter(void (*fn)(RCpu *, uint32_t));
void rrw_mipsi_call(RCpu *c, uint32_t addr);
void rrw_rc_set_hle(const RcFunc *table, unsigned n);
void *rrw_psx_gte_get_context(void);
extern PsxGte *rrw_rc_gte;
extern int64_t rrw_rc_budget;
extern double rrw_ac3_rr_t[6];

static RrSnap g_snap;

static int prep(uint32_t ot) {
    (void)ot;
    return ac3_rr_snapshot(&g_snap);
}

static int build(uint32_t ot, float t, const uint8_t *ram, const uint8_t *scr, uint8_t **ram_out, uint8_t **scr_out) {
    memcpy(rrw_psx_ram, ram, PSX_RAM_SIZE);
    memcpy(rrw_psx_scratch, scr, 0x400);
    rrw_rc_budget = INT64_MAX;   /* sin reloj: en la copia no hay VBlank */
    *ram_out = rrw_psx_ram;
    *scr_out = rrw_psx_scratch;
    return rrw_ac3_rr_build_from(&g_snap, ot, t);
}

static void done(void) { ac3_rr_release(); }

static const PsxInterpRebuildAsync g_async = {prep, build, done};
const PsxInterpRebuildAsync *ac3_rr_async(void) { return &g_async; }

int ac3_rr_async_install(const Ac3Version *v) {
    int i = (int)(v - ac3_versions);
    if (i < 0 || i >= ac3_nversions) return -1;
    const Ac3Version *w = &rrw_ac3_versions[i];
    rrw_rc_set_region(w->ovl_base, w->lib_lo, w->lib_hi, w->lib_main, w->gp);
    rrw_rc_set_interpreter(rrw_mipsi_call);
    rrw_rc_set_hle(w->hle, *w->nhle);
    rrw_rc_gte = (PsxGte *)rrw_psx_gte_get_context();
    rrw_ac3_wide_install(w);   /* los enganches de la pantalla ancha, en la copia (psx_gte_wide viene en la instantánea) */
    return rrw_ac3_rr_install(w);
}

void ac3_rr_async_times(double t[6]) { memcpy(t, rrw_ac3_rr_t, 6 * sizeof t[0]); }
