/* Pruebas del emulador de la GTE (psx/src/gte.c): valores calculados a mano y
 * coherencia interna. No sustituyen a una verificacion contra hardware.
 *   cc -O2 -Ipsx/include psx/test/test_gte.c psx/src/gte.c -lm -o psx/build/test_gte */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "psx_gte.h"

static int fails, checks;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        checks++;                                           \
        if (!(cond)) {                                      \
            fails++;                                        \
            printf("FALLO %s:%d: ", __FILE__, __LINE__);    \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

static void set_rt_identity(PsxGte *g) {
    psx_gte_write_ctrl(g, 0, 0x1000);            /* R11 = 1.0, R12 = 0 */
    psx_gte_write_ctrl(g, 1, 0);                 /* R13, R21 */
    psx_gte_write_ctrl(g, 2, 0x1000);            /* R22, R23 */
    psx_gte_write_ctrl(g, 3, 0);                 /* R31, R32 */
    psx_gte_write_ctrl(g, 4, 0x1000);            /* R33 */
}

static void set_v(PsxGte *g, int n, int x, int y, int z) {
    psx_gte_write_data(g, 2 * n, ((uint32_t)(y & 0xFFFF) << 16) | (x & 0xFFFF));
    psx_gte_write_data(g, 2 * n + 1, (uint32_t)(z & 0xFFFF));
}

static int16_t sx(PsxGte *g, int k) { return (int16_t)(psx_gte_read_data(g, 12 + k) & 0xFFFF); }
static int16_t sy(PsxGte *g, int k) { return (int16_t)(psx_gte_read_data(g, 12 + k) >> 16); }

static void test_unr(void) {
    /* La division de la GTE es una aproximacion por diseno (psx-spx, "GTE Division
     * Inaccuracy"): se espera que casi siempre coincida con round(h * 65536 / z) y que se
     * aparte unas pocas unidades con cocientes grandes. */
    int worst = 0, n = 0, exact = 0;
    for (uint32_t z = 1; z < 0x10000; z += 7)
        for (uint32_t h = 0; h < 2 * z && h < 0x10000; h += 1 + h / 3) {
            double ex = floor((double)h * 65536.0 / z + 0.5);
            if (ex > 0x1FFFF) ex = 0x1FFFF;
            int d = (int)psx_gte_unr_divide(h, z) - (int)ex;
            if (abs(d) > worst) worst = abs(d);
            exact += d == 0;
            n++;
        }
    CHECK(worst <= 3 && exact > n * 0.9, "division UNR: peor error %d, exactos %d de %d", worst, exact, n);
    CHECK(psx_gte_unr_divide(1000, 1000) == 0x10000, "1000/1000 = %u", psx_gte_unr_divide(1000, 1000));
    CHECK(psx_gte_unr_divide(2000, 1000) == 0x1FFFF, "desbordamiento -> 0x1FFFF");
    printf("  division UNR: %d casos, %.1f%% exactos, error maximo %d (aproximacion del hardware)\n", n,
           100.0 * exact / n, worst);
}

static void test_rtps(void) {
    PsxGte g;
    psx_gte_reset(&g);
    set_rt_identity(&g);
    psx_gte_write_ctrl(&g, 26, 1000);            /* H */
    set_v(&g, 0, 100, 50, 1000);
    psx_gte_command(&g, GTE_RTPS | GTE_SF);
    CHECK(sx(&g, 2) == 100 && sy(&g, 2) == 50, "RTPS identidad: (%d, %d) esperado (100, 50)", sx(&g, 2), sy(&g, 2));
    CHECK(psx_gte_read_data(&g, 19) == 1000, "SZ3 = %u", psx_gte_read_data(&g, 19));
    CHECK((int32_t)psx_gte_read_data(&g, 9) == 100, "IR1 = %d", (int32_t)psx_gte_read_data(&g, 9));
    CHECK(!(psx_gte_read_ctrl(&g, 31) & 0x80000000u), "sin banderas de error: FLAG %08x", psx_gte_read_ctrl(&g, 31));
    /* desplazamiento de pantalla (OFX, OFY en 16.16) y traslacion */
    psx_gte_write_ctrl(&g, 24, 160 << 16);
    psx_gte_write_ctrl(&g, 25, 120 << 16);
    psx_gte_write_ctrl(&g, 7, 1000);             /* TRZ: el punto queda a z = 2000 */
    psx_gte_command(&g, GTE_RTPS | GTE_SF);
    CHECK(sx(&g, 2) == 160 + 50 && sy(&g, 2) == 120 + 25, "RTPS con OF/TR: (%d, %d) esperado (210, 145)", sx(&g, 2),
          sy(&g, 2));
    /* saturacion de la pantalla: x muy grande */
    set_v(&g, 0, 30000, 0, 100);
    psx_gte_write_ctrl(&g, 7, 0);
    psx_gte_command(&g, GTE_RTPS | GTE_SF);
    CHECK(sx(&g, 2) == 0x3FF && (psx_gte_read_ctrl(&g, 31) & (1u << 14)), "SX2 saturado a 0x3FF con bandera");
    /* detras de la camara: z <= 0 -> division desbordada */
    set_v(&g, 0, 10, 10, -50);
    psx_gte_command(&g, GTE_RTPS | GTE_SF);
    CHECK(psx_gte_read_ctrl(&g, 31) & (1u << 17), "z negativa: bandera de division");
}

static void test_rtpt_equals_rtps(void) {
    PsxGte a, b;
    psx_gte_reset(&a);
    srand(7);
    int bad = 0;
    for (int it = 0; it < 2000; it++) {
        psx_gte_reset(&a);
        for (int r = 0; r < 5; r++) psx_gte_write_ctrl(&a, r, (uint32_t)rand() * 2654435761u);
        for (int r = 5; r < 8; r++) psx_gte_write_ctrl(&a, r, (uint32_t)(rand() % 20000 - 10000));
        psx_gte_write_ctrl(&a, 24, (uint32_t)(rand() % 640) << 15);
        psx_gte_write_ctrl(&a, 25, (uint32_t)(rand() % 480) << 15);
        psx_gte_write_ctrl(&a, 26, (uint32_t)(rand() % 1000 + 100));
        psx_gte_write_ctrl(&a, 27, (uint32_t)(rand() % 2000 - 1000));
        psx_gte_write_ctrl(&a, 28, (uint32_t)rand());
        for (int n = 0; n < 3; n++) set_v(&a, n, rand() % 4000 - 2000, rand() % 4000 - 2000, rand() % 4000 - 2000);
        b = a;
        psx_gte_command(&a, GTE_RTPT | GTE_SF);
        /* tres RTPS con cada vertice movido a V0 */
        uint32_t v[6];
        for (int k = 0; k < 6; k++) v[k] = psx_gte_read_data(&b, k);
        for (int n = 0; n < 3; n++) {
            psx_gte_write_data(&b, 0, v[2 * n]);
            psx_gte_write_data(&b, 1, v[2 * n + 1]);
            psx_gte_command(&b, GTE_RTPS | GTE_SF);
        }
        for (int r = 12; r <= 19; r++)
            if (r != 15 && psx_gte_read_data(&a, r) != psx_gte_read_data(&b, r)) { bad++; break; }
    }
    CHECK(bad == 0, "RTPT != 3 x RTPS en %d de 2000 casos", bad);
    printf("  RTPT frente a 3 x RTPS: 2000 casos aleatorios, %d diferencias en SXY/SZ\n", bad);
}

static void test_nclip_avsz(void) {
    PsxGte g;
    psx_gte_reset(&g);
    psx_gte_write_data(&g, 12, (0u << 16) | 0);         /* (0, 0) */
    psx_gte_write_data(&g, 13, (0u << 16) | 10);        /* (10, 0) */
    psx_gte_write_data(&g, 14, (10u << 16) | 0);        /* (0, 10) */
    psx_gte_command(&g, GTE_NCLIP);
    CHECK((int32_t)psx_gte_read_data(&g, 24) == 100, "NCLIP = %d esperado 100", (int32_t)psx_gte_read_data(&g, 24));
    for (int k = 0; k < 4; k++) psx_gte_write_data(&g, 16 + k, (uint32_t)(1000 * (k + 1)));
    psx_gte_write_ctrl(&g, 29, 0x1000 / 3);
    psx_gte_command(&g, GTE_AVSZ3);
    /* (2000 + 3000 + 4000) * 1365 >> 12 = 2999 */
    CHECK(psx_gte_read_data(&g, 7) == 2999, "AVSZ3 OTZ = %u esperado 2999", psx_gte_read_data(&g, 7));
    psx_gte_write_ctrl(&g, 30, 0x1000 / 4);
    psx_gte_command(&g, GTE_AVSZ4);
    CHECK(psx_gte_read_data(&g, 7) == 2500, "AVSZ4 OTZ = %u esperado 2500", psx_gte_read_data(&g, 7));
}

static void test_mvmva_vs_float(void) {
    PsxGte g;
    srand(11);
    int worst = 0;
    for (int it = 0; it < 2000; it++) {
        psx_gte_reset(&g);
        double ang = (rand() % 4096) * 2 * M_PI / 4096;
        int c = (int)lround(cos(ang) * 4096), s = (int)lround(sin(ang) * 4096);
        /* rotacion Y: [c 0 s; 0 1 0; -s 0 c] */
        psx_gte_write_ctrl(&g, 0, (uint32_t)(c & 0xFFFF));
        psx_gte_write_ctrl(&g, 1, (uint32_t)(s & 0xFFFF));
        psx_gte_write_ctrl(&g, 2, 0x1000);
        psx_gte_write_ctrl(&g, 3, (uint32_t)(-s & 0xFFFF));
        psx_gte_write_ctrl(&g, 4, (uint32_t)(c & 0xFFFF));
        int tx = rand() % 2000 - 1000, ty = rand() % 2000 - 1000, tz = rand() % 2000 - 1000;
        psx_gte_write_ctrl(&g, 5, (uint32_t)tx);
        psx_gte_write_ctrl(&g, 6, (uint32_t)ty);
        psx_gte_write_ctrl(&g, 7, (uint32_t)tz);
        int x = rand() % 8000 - 4000, y = rand() % 8000 - 4000, z = rand() % 8000 - 4000;
        set_v(&g, 0, x, y, z);
        psx_gte_command(&g, GTE_MVMVA | GTE_SF | GTE_MVMVA_ARGS(0, 0, 0));
        double ex[3] = {tx + (c * x + s * z) / 4096.0, ty + y, tz + (-s * x + c * z) / 4096.0};
        for (int i = 0; i < 3; i++) {
            int d = abs((int32_t)psx_gte_read_data(&g, 25 + i) - (int)floor(ex[i]));
            if (d > worst) worst = d;
        }
    }
    CHECK(worst <= 1, "MVMVA frente a coma flotante: error maximo %d", worst);
    printf("  MVMVA (RT * V0 + TR) frente a coma flotante: 2000 casos, error maximo %d\n", worst);
}

static void test_color(void) {
    PsxGte g;
    psx_gte_reset(&g);
    /* INTPL: IR0 = 0 -> el propio IR; IR0 = 0x1000 -> el color lejano */
    psx_gte_write_ctrl(&g, 21, 0x800);   /* FC = (0x800, 0x400, 0) */
    psx_gte_write_ctrl(&g, 22, 0x400);
    psx_gte_write_ctrl(&g, 23, 0);
    psx_gte_write_data(&g, 6, 0x00000000);
    for (int k = 1; k <= 3; k++) psx_gte_write_data(&g, 8 + k, 0x100);
    psx_gte_write_data(&g, 8, 0);
    psx_gte_command(&g, GTE_INTPL | GTE_SF);
    CHECK((int32_t)psx_gte_read_data(&g, 9) == 0x100, "INTPL IR0=0: IR1 = %x", psx_gte_read_data(&g, 9));
    for (int k = 1; k <= 3; k++) psx_gte_write_data(&g, 8 + k, 0x100);
    psx_gte_write_data(&g, 8, 0x1000);
    psx_gte_command(&g, GTE_INTPL | GTE_SF);
    CHECK((int32_t)psx_gte_read_data(&g, 9) == 0x800 && (int32_t)psx_gte_read_data(&g, 10) == 0x400,
          "INTPL IR0=1: IR1 = %x IR2 = %x", psx_gte_read_data(&g, 9), psx_gte_read_data(&g, 10));
    /* color empujado = MAC >> 4 saturado; codigo de RGBC conservado */
    /* color empujado = MAC >> 4: (0x800, 0x400, 0) -> (0x80, 0x40, 0) */
    CHECK((psx_gte_read_data(&g, 22) & 0xFFFFFF) == (0x80u | (0x40u << 8)),
          "RGB2 = %06x esperado 004080", psx_gte_read_data(&g, 22) & 0xFFFFFF);
    /* SQR */
    psx_gte_write_data(&g, 9, 0x100);
    psx_gte_write_data(&g, 10, (uint32_t)-0x200);
    psx_gte_write_data(&g, 11, 0x7FFF);
    psx_gte_command(&g, GTE_SQR | GTE_SF);
    CHECK((int32_t)psx_gte_read_data(&g, 9) == 0x10 && (int32_t)psx_gte_read_data(&g, 10) == 0x40,
          "SQR: IR1 %x IR2 %x", psx_gte_read_data(&g, 9), psx_gte_read_data(&g, 10));
    /* sin sf, 0x7FFF^2 satura IR3 con su bandera (bit 22), que NO activa el bit 31: el bit
     * 31 es el OR de los bits 30-23 y 18-13 (psx-spx) */
    psx_gte_write_data(&g, 11, 0x7FFF);
    psx_gte_command(&g, GTE_SQR);
    CHECK((int32_t)psx_gte_read_data(&g, 11) == 0x7FFF && (psx_gte_read_ctrl(&g, 31) & (1u << 22)) &&
              !(psx_gte_read_ctrl(&g, 31) & 0x80000000u),
          "SQR sin sf: IR3 saturado, bandera 22 sin bit 31 (FLAG %08x)", psx_gte_read_ctrl(&g, 31));
    /* en cambio la saturacion de IR1 (bit 24) si activa el bit 31 */
    psx_gte_write_data(&g, 9, 0x7FFF);
    psx_gte_command(&g, GTE_SQR);
    CHECK((psx_gte_read_ctrl(&g, 31) & (1u << 24)) && (psx_gte_read_ctrl(&g, 31) & 0x80000000u),
          "SQR sin sf: IR1 saturado activa el bit 31 (FLAG %08x)", psx_gte_read_ctrl(&g, 31));
}

static void test_registers(void) {
    PsxGte g;
    psx_gte_reset(&g);
    psx_gte_write_data(&g, 30, 0x00000F00);
    CHECK(psx_gte_read_data(&g, 31) == 20, "LZCR(0xF00) = %u esperado 20", psx_gte_read_data(&g, 31));
    psx_gte_write_data(&g, 30, 0xFFFF0000);
    CHECK(psx_gte_read_data(&g, 31) == 16, "LZCR(0xFFFF0000) = %u esperado 16", psx_gte_read_data(&g, 31));
    psx_gte_write_data(&g, 28, (31u << 10) | (16u << 5) | 1u);
    CHECK((int32_t)psx_gte_read_data(&g, 11) == 31 * 0x80 && (int32_t)psx_gte_read_data(&g, 10) == 16 * 0x80,
          "IRGB -> IR");
    CHECK(psx_gte_read_data(&g, 29) == ((31u << 10) | (16u << 5) | 1u), "ORGB = %x", psx_gte_read_data(&g, 29));
    psx_gte_write_data(&g, 15, (7u << 16) | 3u);
    CHECK(psx_gte_read_data(&g, 14) == ((7u << 16) | 3u), "SXYP empuja a SXY2");
    psx_gte_write_ctrl(&g, 26, 0x8000);
    CHECK(psx_gte_read_ctrl(&g, 26) == 0xFFFF8000u, "H se lee con signo (fallo del hardware)");
}

int main(void) {
    test_unr();
    test_rtps();
    test_rtpt_equals_rtps();
    test_nclip_avsz();
    test_mvmva_vs_float();
    test_color();
    test_registers();
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
