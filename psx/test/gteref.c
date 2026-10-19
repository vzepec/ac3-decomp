/* Comparador de la GTE con una de referencia en partida real (etapa 6, paso 18).
 *
 * La GTE de psx/src/gte.c se escribio desde la especificacion (psx-spx) y solo se habia
 * comprobado con pruebas propias, no contra hardware. Aqui cada orden COP2 que ejecuta el juego
 * se repite en la GTE de Mednafen (la de Beetle PSX, contrastada durante anos con hardware real),
 * con el mismo estado de entrada (los 64 registros leidos con MFC2/CFC2), y se comparan los 32
 * registros de datos y FLAG. La de Mednafen es GPL: no esta en el repositorio; `make gteref` la
 * descarga a data/ref (fuera de git) y enlaza este archivo con ella en un binario de pruebas
 * aparte, que nunca entra en el APK.
 *
 * Salida: las primeras diferencias de cada orden (con su estado de entrada) y, al salir, el
 * resumen: ordenes ejecutadas y diferentes por codigo, y que registros difieren. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gte.h"

void GTE_Init(void);
void GTE_Power(void);
int32_t GTE_Instruction(uint32_t instr);
void GTE_WriteCR(unsigned int which, uint32_t value);
void GTE_WriteDR(unsigned int which, uint32_t value);
uint32_t GTE_ReadCR(unsigned int which);
uint32_t GTE_ReadDR(unsigned int which);

static long g_ops[64], g_bad[64], g_reg_bad[64][33];
static uint32_t g_in_d[32], g_in_c[32];
static int g_shown[64], g_lzcr_unset;
int VSync(int);

static const char *NAMES[64] = {[0x01] = "RTPS", [0x06] = "NCLIP", [0x0C] = "OP", [0x10] = "DPCS", [0x11] = "INTPL",
                                [0x12] = "MVMVA", [0x13] = "NCDS", [0x14] = "CDP", [0x16] = "NCDT", [0x1B] = "NCCS",
                                [0x1C] = "CC", [0x1E] = "NCS", [0x20] = "NCT", [0x28] = "SQR", [0x29] = "DCPL",
                                [0x2A] = "DPCT", [0x2D] = "AVSZ3", [0x2E] = "AVSZ4", [0x30] = "RTPT", [0x3D] = "GPF",
                                [0x3E] = "GPL", [0x3F] = "NCCT"};

static void hook(PsxGte *g, uint32_t cmd, int after) {
    if (!after) {   /* el mismo estado de entrada en la de referencia */
        for (int r = 0; r < 32; r++) g_in_c[r] = psx_gte_read_ctrl(g, r);
        for (int r = 0; r < 32; r++) g_in_d[r] = psx_gte_read_data(g, r);
        for (int r = 0; r < 32; r++) GTE_WriteCR((unsigned)r, g_in_c[r]);
        for (int r = 0; r < 32; r++)
            if (r != 15 && r != 28 && r != 29 && r != 31) GTE_WriteDR((unsigned)r, g_in_d[r]);
        g_lzcr_unset = GTE_ReadDR(31) != g_in_d[31];
        return;
    }
    GTE_Instruction(cmd);
    int op = cmd & 63, bad = 0;
    uint32_t mine[33], ref[33];
    for (int r = 0; r < 32; r++) { mine[r] = psx_gte_read_data(g, r); ref[r] = GTE_ReadDR((unsigned)r); }
    mine[32] = psx_gte_read_ctrl(g, 31); ref[32] = GTE_ReadCR(31);
    for (int r = 0; r < 33; r++) {
        if (r == 15 || mine[r] == ref[r]) continue;   /* 15 (SXYP) refleja a SXY2 */
        /* LZCR (31) sale de LZCS al escribirlo; la nuestra arranca a 0 sin haberlo escrito */
        if (r == 31 && g_lzcr_unset) continue;
        g_reg_bad[op][r]++;
        bad = 1;
    }
    g_ops[op]++;
    if (!bad) return;
    g_bad[op]++;
    if (getenv("GTEREF_REG")) {   /* solo los ejemplos que difieren en ese registro (d8...) */
        int want = atoi(getenv("GTEREF_REG"));
        if (mine[want] == ref[want]) return;
    }
    if (g_shown[op]++ >= 4) return;
    printf("gteref: VBlank %d, %s (%07x) distinta:", VSync(-1), NAMES[op] ? NAMES[op] : "?", cmd);
    for (int r = 0; r < 33; r++)
        if (r != 15 && mine[r] != ref[r]) printf(" %s%d %08x/%08x", r == 32 ? "FLAG" : "d", r == 32 ? 0 : r, mine[r], ref[r]);
    printf("\n  entrada d:");
    for (int r = 0; r < 32; r++) printf(" %08x", g_in_d[r]);
    printf("\n  entrada c:");
    for (int r = 0; r < 32; r++) printf(" %08x", g_in_c[r]);
    printf("\n");
}

static void summary(void) {
    long n = 0, b = 0;
    for (int op = 0; op < 64; op++) { n += g_ops[op]; b += g_bad[op]; }
    printf("gteref: %ld ordenes, %ld distintas\n", n, b);
    for (int op = 0; op < 64; op++) {
        if (!g_ops[op]) continue;
        printf("gteref:   %-6s %9ld ordenes, %8ld distintas", NAMES[op] ? NAMES[op] : "?", g_ops[op], g_bad[op]);
        for (int r = 0; r < 33; r++)
            if (g_reg_bad[op][r]) printf("  %s%d:%ld", r == 32 ? "FLAG" : "d", r == 32 ? 0 : r, g_reg_bad[op][r]);
        printf("\n");
    }
}

void psx_gteref_start(void) {
    GTE_Init();
    GTE_Power();
    psx_gte_cmd_hook = hook;
    atexit(summary);
}
