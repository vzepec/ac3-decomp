/* Prueba de las lineas del dibujo por GLES (psx/gl/psx_hw.c) frente a la GPU por software (etapa
 * 6, paso 17). Dibuja una a una lineas de todas las pendientes y sentidos (extremos con
 * desplazamientos de -20..20 desde varios origenes), cada una sobre fondo negro, por los dos
 * caminos a escala 1, y cuenta las que no salen pixel a pixel iguales. Sale 0 si todas coinciden.
 *   test_hwlines [-v] */
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "../gl/psx_hw.h"

int main(int argc, char **argv) {
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    const EGLint attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                           EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint nc;
    eglChooseConfig(dpy, attr, &cfg, 1, &nc);
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
    const EGLint cattr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattr);
    if (!eglMakeCurrent(dpy, surf, surf, ctx) || psx_hw_init()) { puts("sin contexto GLES"); return 1; }

    static PsxGpu g;
    static PsxHwPrim prims[16];
    static uint16_t data[4096], bg[1024 * 512];
    static uint32_t hw[64 * 64];
    const int W = 64, H = 64;
    int total = 0, bad = 0;
    const int org[3][2] = {{32, 32}, {31, 30}, {33, 29}};
    for (int o = 0; o < 3; o++)
        for (int dy = -20; dy <= 20; dy++)
            for (int dx = -20; dx <= 20; dx++) {
                psx_gpu_reset(&g);
                psx_gpu_gp0(&g, 0xE3000000u);                                  /* area 0,0 - 63,63 */
                psx_gpu_gp0(&g, 0xE4000000u | (uint32_t)(H - 1) << 10 | (uint32_t)(W - 1));
                psx_gpu_gp0(&g, 0xE5000000u);
                memset(g.vram, 0, sizeof g.vram);
                memcpy(bg, g.vram, sizeof bg);
                PsxHwList l = {.p = prims, .max = 16, .data = data, .maxdata = 4096, .main_x1 = 0, .main_y1 = 0,
                               .main_x2 = W - 1, .main_y2 = H - 1};
                int ax = org[o][0], ay = org[o][1], bx = ax + dx, by = ay + dy;
                psx_gpu_record(&l, 0);
                psx_gpu_gp0(&g, 0x40FFFFFFu);   /* linea plana, blanca */
                psx_gpu_gp0(&g, (uint32_t)(uint16_t)ay << 16 | (uint16_t)ax);
                psx_gpu_gp0(&g, (uint32_t)(uint16_t)by << 16 | (uint16_t)bx);
                psx_gpu_record_flush(&g);
                psx_gpu_record(NULL, 0);
                psx_hw_frame(&l, bg, 0, 0, W, H, 1);
                psx_hw_read(hw);
                int diff = 0;
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < W; x++) diff += ((g.vram[y * 1024 + x] & 0x7FFF) != 0) != ((hw[y * W + x] & 0xFF) != 0);
                total++;
                if (diff) {
                    bad++;
                    if (verbose) printf("linea (%d,%d)-(%d,%d): %d pixeles distintos\n", ax, ay, bx, by, diff);
                }
            }
    printf("%d lineas, %d distintas\n", total, bad);
    psx_hw_shutdown();
    return bad != 0;
}
