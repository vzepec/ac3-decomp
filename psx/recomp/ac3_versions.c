/* Tabla de versiones del modo libre (ver ac3_versions.h). Direcciones de la EU: las de la etapa
 * 6 (docs/11-etapa6.md); de la US: tools/recomp/region.py (data/recomp_us/region.json),
 * validadas con 0 funciones distintas en el comparador. Estado de la partida y arranque de
 * mision: decompilacion de Ghidra (EU FUN_800355f0 / US FUN_800357b8 leen mission_records
 * con el short de g_state + 0x390; docs/11-etapa6.md, paso 11). */
#include "ac3_versions.h"
#include <stdio.h>
#include <stdlib.h>
#include "psx_gte.h"
#include "psx_mem.h"
#include "psx_libcd.h"

extern const RcFunc hle_table_eu[], hle_table_us[];
extern const unsigned hle_count_eu, hle_count_us;

const Ac3Version ac3_versions[] = {
    {"EU", "\\SCES_020.66;1", 0x80025D08u, 0x800BA9F4u, 0x800BACB0u, 0x800C9DC0u, 0x800C9DC0u, 0x80099DE0u, 0x800AE09Cu,
     0x80025214u, 0x800BAA04u, 0x800BACD0u, 0x800355F0u, 0x800563C8u, 0x8005E140u, 0x8005FD68u, 0x80092F90u, 50, hle_table_eu, &hle_count_eu},
    {"US", "\\SLUS_009.72;1", 0x80025E5Cu, 0x800BB454u, 0x800BB808u, 0x800CA908u, 0x800CA908u, 0x8009A1B4u, 0x800AE470u,
     0x80025394u, 0x800BB464u, 0x800BB828u, 0x800357B8u, 0x8005658Cu, 0x8005E3E4u, 0x8006000Cu, 0x80093324u, 60, hle_table_us, &hle_count_us},
};
const int ac3_nversions = 2;

const Ac3Version *ac3_version_detect(void) {
    for (int i = 0; i < ac3_nversions; i++) {
        CdlFILE f;
        if (CdSearchFile(&f, ac3_versions[i].boot_exe)) return &ac3_versions[i];
    }
    return 0;
}

/* ---- pantalla ancha: el ancho de pantalla de la camara (short en a0 + 4 de la funcion del cono
 * de vision, 320; EU FUN_8005e140, US FUN_8005e3e4) se cambia al entrar y se devuelve al entrar
 * en la funcion que la sigue en FUN_8005fe48 (EU FUN_8005fd68, US FUN_8006000c) ---- */
volatile uint32_t ac3_terrain_count;
static uint32_t g_wide_addr;
static uint16_t g_wide_orig;
static void wide_enter(RCpu *c) {
    ac3_terrain_count++;
    g_wide_addr = 0;
    if (psx_gte_wide >= 1.0f || psx_gte_wide <= 0.0f) return;
    uint32_t a = c->r[4] + 4;
    if ((a & 0x1FFFFFu) >= PSX_RAM_SIZE - 4) return;
    uint16_t *w = (uint16_t *)psx_ptr(a);
    static int said;
    if (getenv("AC3_WIDE_DBG") && said++ < 3) fprintf(stderr, "pantalla ancha: ancho %d, alto %d\n", w[0], w[1]);
    g_wide_addr = a;
    g_wide_orig = *w;
    *w = (uint16_t)((float)*w / psx_gte_wide + 0.5f);
}
static void wide_leave(RCpu *c) {
    (void)c;
    if (g_wide_addr) *(uint16_t *)psx_ptr(g_wide_addr) = g_wide_orig;
    g_wide_addr = 0;
}
/* las lineas del horizonte del cielo (US FUN_80093324, EU FUN_80092f90; las llaman US
 * FUN_80093b08 y FUN_80094c94 con a0 = 0x1f80001c, a1 = numero de lineas): cada una son dos
 * puntos (x = -X y +X, short en a0 + 0x2c + 8k y + 0x30 + 8k) que la GTE proyecta (RTPT) y que se
 * recortan al rectangulo de la pantalla (US FUN_8005dd8c, +-160 x +-120). X esta calculado para
 * que, en 4:3, los extremos caigan fuera de la pantalla (US FUN_8009354c: 0x3bff88 / H, a +-240
 * px; FUN_8009497c: a +-200). Con la x comprimida, al ladear el avion pueden quedar dentro: se
 * alargan antes de proyectar. Quien llama los recalcula en cada imagen */
static void wide_sky(RCpu *c) {
    static int off = -1;   /* prueba: AC3_WIDE_NOSKY=1, sin la correccion */
    if (off < 0) off = getenv("AC3_WIDE_NOSKY") != NULL;
    if (off || psx_gte_wide >= 1.0f || psx_gte_wide <= 0.0f) return;
    uint32_t a = c->r[4];
    int n = (int)c->r[5];
    if ((a & 0xFFFFFC00u) != 0x1F800000u || n < 1 || n > 8) return;
    for (int k = 0; k < n; k++)
        for (int j = 0; j < 2; j++) {
            int16_t *x = (int16_t *)psx_ptr(a + 0x2c + 8 * k + 4 * j);
            float v = (float)*x / psx_gte_wide;
            *x = (int16_t)(v > 32767.0f ? 32767 : v < -32767.0f ? -32767 : v);
        }
}
void ac3_wide_install(const Ac3Version *v) {
    if (v->sky_lines) mipsi_set_hook(v->sky_lines, wide_sky);
    mipsi_set_hook(v->frustum, wide_enter);
    mipsi_set_hook(v->frustum_next, wide_leave);
}
