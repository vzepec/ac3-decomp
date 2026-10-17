/* Pruebas de la GPU por software (psx/src/gpu.c).
 *   psx/build/test_gpu [archivos .tim...]
 * Con archivos TIM (extraidos del disco por tools/psyqcheck.py o a mano), cada uno se sube
 * a la VRAM y se dibuja como sprite; cada pixel debe coincidir con la decodificacion
 * independiente del TIM hecha aqui. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx_gpu.h"
#include "psx_mem.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static PsxGpu G;

static void send(const uint32_t *w, int n) { for (int i = 0; i < n; i++) psx_gpu_gp0(&G, w[i]); }
static uint16_t px(int x, int y) { return G.vram[y * PSX_VRAM_W + x]; }

static void test_fill_rules(void) {
    psx_gpu_reset(&G);
    uint32_t q[] = {0x28FFFFFFu, 0x00000000u, 0x0000000Au, 0x000A0000u, 0x000A000Au};   /* quad plano blanco */
    send(q, 5);
    int count = 0, outside = 0;
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++)
            if (px(x, y)) { count++; if (x > 9 || y > 9) outside++; }
    CHECK(count == 100 && outside == 0, "quad 10x10: %d pixeles (%d fuera de 0..9)", count, outside);
    /* dos triangulos que comparten arista, en mezcla aditiva: ni huecos ni solapes */
    psx_gpu_reset(&G);
    uint32_t e1 = 0xE1000020u;   /* abr = 1 (B + F) */
    send(&e1, 1);
    uint32_t t1[] = {0x22080808u, 0x00000000u, 0x00000014u, 0x00140000u};   /* (0,0) (20,0) (0,20) */
    uint32_t t2[] = {0x22080808u, 0x00000014u, 0x00140014u, 0x00140000u};   /* (20,0) (20,20) (0,20) */
    send(t1, 4);
    send(t2, 4);
    int holes = 0, doubles = 0;
    for (int y = 0; y < 20; y++)
        for (int x = 0; x < 20; x++) {
            int r = px(x, y) & 31;
            if (r == 0) holes++;
            if (r > 1) doubles++;
        }
    CHECK(holes == 0 && doubles == 0, "arista compartida: %d huecos, %d pixeles dibujados dos veces", holes, doubles);
}

static void test_gouraud_and_semi(void) {
    psx_gpu_reset(&G);
    uint32_t t[] = {0x300000F8u, 0x00000000u, 0x0000F800u, 0x00000040u, 0x00F80000u, 0x00400000u};
    send(t, 6);
    uint16_t c0 = px(0, 0);
    CHECK((c0 & 31) == 31 && ((c0 >> 5) & 31) == 0 && ((c0 >> 10) & 31) == 0, "Gouraud: vertice rojo = %04x", c0);
    /* modos de mezcla sobre un fondo (16,16,16) con color (8,8,8) en 5 bits */
    int expect[4] = {12, 24, 8, 18};
    for (int abr = 0; abr < 4; abr++) {
        psx_gpu_reset(&G);
        psx_gpu_fill(&G, 0, 0, 16, 16, 128, 128, 128);
        uint32_t e1 = 0xE1000000u | (uint32_t)abr << 5;
        send(&e1, 1);
        uint32_t r[] = {0x62404040u, 0x00000000u, 0x00040004u};   /* rect semitransparente 4x4, color 64 -> 8 */
        send(r, 3);
        int got = px(1, 1) & 31;
        CHECK(got == expect[abr], "mezcla %d: %d esperado %d", abr, got, expect[abr]);
    }
}

static void test_transfer_and_list(void) {
    psx_gpu_reset(&G);
    uint32_t a0[] = {0xA0000000u, (5u << 16) | 3u, (2u << 16) | 4u, 0x22221111u, 0x44443333u, 0x66665555u, 0x88887777u};
    send(a0, 7);
    CHECK(px(3, 5) == 0x1111 && px(6, 5) == 0x4444 && px(3, 6) == 0x5555 && px(6, 6) == 0x8888, "GP0 A0 a VRAM");
    /* GP0 80: copia de VRAM a VRAM (MoveImage) del bloque 4x2 a (100, 50) */
    uint32_t c80[] = {0x80000000u, (5u << 16) | 3u, (50u << 16) | 100u, (2u << 16) | 4u};
    send(c80, 4);
    CHECK(px(100, 50) == 0x1111 && px(103, 50) == 0x4444 && px(100, 51) == 0x5555 && px(103, 51) == 0x8888 &&
          px(104, 50) == 0 && px(3, 5) == 0x1111, "GP0 80 VRAM a VRAM");
    /* lista de dibujo en la RAM de la PS1: dos paquetes encadenados */
    psx_gpu_reset(&G);
    uint32_t *p1 = (uint32_t *)psx_ptr(0x80100000u), *p2 = (uint32_t *)psx_ptr(0x80100100u);
    p1[0] = (3u << 24) | 0x100100u;            /* 3 palabras, siguiente: p2 */
    p1[1] = 0x60FF0000u; p1[2] = 0x00000000u; p1[3] = 0x00020002u;   /* rect azul 2x2 en (0,0) */
    p2[0] = (3u << 24) | 0xFFFFFFu;            /* ultimo */
    p2[1] = 0x6000FF00u; p2[2] = 0x00000004u; p2[3] = 0x00020002u;   /* rect verde 2x2 en (4,0) */
    psx_gpu_draw_list(&G, 0x80100000u);
    CHECK(px(0, 0) == (31u << 10) && px(4, 0) == (31u << 5) && G.prims_drawn == 2, "lista de dibujo: %04x %04x, %u prims",
          px(0, 0), px(4, 0), G.prims_drawn);
}

/* ---- texturas reales: TIM -> VRAM -> sprite -> comparacion con el TIM ---- */
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

static void test_tim(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("no se abre %s\n", path); return; }
    static uint8_t b[1 << 20];
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n < 20 || rd32(b) != 0x10) return;
    uint32_t flags = rd32(b + 4), depth = flags & 7;
    if (depth > 1 || !(flags & 8)) return;     /* solo 4 y 8 bits con CLUT */
    const uint8_t *cb = b + 8;
    int cx = rd16(cb + 4), cy = rd16(cb + 6), cw = rd16(cb + 8), ch = rd16(cb + 10);
    const uint8_t *ib = cb + rd32(cb);
    int ix = rd16(ib + 4), iy = rd16(ib + 6), iw = rd16(ib + 8), ih = rd16(ib + 10);
    if (ix % 64 || (iy % 256) + ih > 256) return;   /* sprite con u desde 0 dentro de una pagina */
    psx_gpu_reset(&G);
    psx_gpu_load_image(&G, cx, cy, cw, ch, (const uint16_t *)(cb + 12));
    psx_gpu_load_image(&G, ix, iy, iw, ih, (const uint16_t *)(ib + 12));
    int wpx = iw * (depth == 0 ? 4 : 2);
    if (wpx > 256) wpx = 256;
    int dy = iy >= 256 ? 0 : 256;   /* dibujar en la otra mitad de la VRAM */
    int dx = 0;
    uint32_t e1 = 0xE1000000u | (depth << 7) | ((uint32_t)(iy / 256) << 4) | (uint32_t)(ix / 64);
    send(&e1, 1);
    uint32_t clut = (uint32_t)((cy << 6) | (cx >> 4));
    uint32_t sp[] = {0x65808080u, ((uint32_t)dy << 16) | (uint32_t)dx, (clut << 16) | ((uint32_t)(iy % 256) << 8),
                     ((uint32_t)ih << 16) | (uint32_t)wpx};
    send(sp, 4);
    int bad = 0;
    for (int y = 0; y < ih; y++)
        for (int x = 0; x < wpx; x++) {
            uint16_t hw = rd16(ib + 12 + 2 * (y * iw + (depth == 0 ? x / 4 : x / 2)));
            int idx = depth == 0 ? (hw >> ((x & 3) * 4)) & 15 : (hw >> ((x & 1) * 8)) & 255;
            uint16_t want = rd16(cb + 12 + 2 * idx);
            uint16_t got = px(dx + x, dy + y);
            if (want == 0 ? got != 0 : got != want) bad++;   /* 0 = transparente: no se dibuja */
        }
    CHECK(bad == 0, "%s: %d pixeles distintos de %d", path, bad, wpx * ih);
    if (!bad) printf("  %s: %dx%d %d bits, identico\n", strrchr(path, '/') ? strrchr(path, '/') + 1 : path, wpx, ih,
                     depth ? 8 : 4);
}

int main(int argc, char **argv) {
    test_fill_rules();
    test_gouraud_and_semi();
    test_transfer_and_list();
    for (int i = 1; i < argc; i++) test_tim(argv[i]);
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
