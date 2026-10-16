/* libgte: funciones matematicas sin GTE (o con solo el contador LZCS/LZCR).
 * Reimplementadas a partir de la descompilacion de ACE.BIN (version EU):
 * rsin 0x800A45C8, rcos 0x800A4698, SquareRoot0 0x800A47C8, ratan2 0x800A6088. */
#include "psx_libgte.h"

#include <math.h>
#include <string.h>

static short sin_tbl[4097];     /* round(4096 * sin(2*pi*i/4096)): exacto frente al original */
static short sqrt_tbl[192];     /* floor(sqrt(v) * 512) para v = 0x40..0xFF: exacto */
static short ratan_tbl[1025];   /* leida del ACE.BIN del usuario */
static int tables_ready, ratan_exact;

static void gen_tables(void) {
    for (int i = 0; i <= 4096; i++) sin_tbl[i] = (short)lround(4096.0 * sin(i * (2.0 * M_PI / 4096.0)));
    for (int v = 0x40; v < 0x100; v++) sqrt_tbl[v - 0x40] = (short)floor(sqrt((double)v) * 512.0);
    for (int i = 0; i <= 1024; i++) ratan_tbl[i] = (short)lround(atan(i / 1024.0) * 2048.0 / M_PI);
    tables_ready = 1;
}

/* Busca la tabla de arcotangente por su estructura (no por su contenido literal):
 * 1025 valores s16, t[0] = 0, t[1024] = 512, no decrecientes y a menos de 1 de
 * atan(i/1024) * 2048/pi. Validado: aparece una sola vez en ACE.BIN EU y US. */
int psx_gte_tables_init(const uint8_t *acebin, size_t len) {
    gen_tables();
    ratan_exact = 0;
    if (!acebin) return -1;
    for (size_t off = 0; off + 2050 <= len; off += 2) {
        const uint8_t *p = acebin + off;
        if (p[0] || p[1] || (p[2048] | p[2049] << 8) != 512) continue;
        int ok = 1, prev = 0;
        for (int i = 0; i <= 1024 && ok; i++) {
            int v = (short)(p[2 * i] | p[2 * i + 1] << 8);
            double x = atan(i / 1024.0) * 2048.0 / M_PI;
            if (v < prev || fabs(v - x) > 1.0) ok = 0;
            prev = v;
        }
        if (!ok) continue;
        for (int i = 0; i <= 1024; i++) ratan_tbl[i] = (short)(p[2 * i] | p[2 * i + 1] << 8);
        ratan_exact = 1;
        return 0;
    }
    return -1;
}

int psx_gte_ratan_exact(void) { return ratan_exact; }

int rsin(int a) {
    if (!tables_ready) gen_tables();
    if (a < 0) return -sin_tbl[(-a) & 0xFFF];
    return sin_tbl[a & 0xFFF];
}

int rcos(int a) {
    if (!tables_ready) gen_tables();
    if (a < 0) a = -a;
    return sin_tbl[((a & 0xFFF) + 1024) & 0xFFF] ;
}

/* Bits iniciales iguales al signo (registro LZCR de la GTE tras escribir LZCS). */
static int lzcr(int32_t v) {
    uint32_t u = v < 0 ? ~(uint32_t)v : (uint32_t)v;
    int n = 0;
    while (n < 32 && !(u & (0x80000000u >> n))) n++;
    return n;
}

long SquareRoot0(long a) {
    if (!tables_ready) gen_tables();
    int32_t x = (int32_t)a;
    int n = lzcr(x);
    if (n == 32) return 0;
    n &= ~1;
    int32_t v = n < 24 ? x >> (24 - n) : (int32_t)((uint32_t)x << (n - 24));
    return (long)(((uint32_t)((int32_t)sqrt_tbl[v - 0x40] << ((31 - n) >> 1))) >> 12);
}

long ratan2(long y, long x) {
    if (!tables_ready) gen_tables();
    int32_t yy = (int32_t)y, xx = (int32_t)x;
    int xneg = xx < 0, yneg = yy < 0;
    if (xneg) xx = -xx;
    if (yneg) yy = -yy;
    if (xx == 0 && yy == 0) return 0;
    int32_t v;
    if (yy < xx) {
        int32_t idx = (yy & 0x7FE00000) ? yy / (xx >> 10) : (int32_t)((uint32_t)yy << 10) / xx;
        v = ratan_tbl[idx];
    } else {
        int32_t idx = (xx & 0x7FE00000) ? xx / (yy >> 10) : (int32_t)((uint32_t)xx << 10) / yy;
        v = 0x400 - ratan_tbl[idx];
    }
    if (xneg) v = 0x800 - v;
    if (yneg) v = -v;
    return v;
}
