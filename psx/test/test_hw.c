/* Prueba del dibujo por GLES (psx/gl/psx_hw.c) frente a la GPU por software (etapa 6, paso 7).
 *   test_hw <volcado.gpu> [salida_sin_extension]
 * El volcado es el de PSX_GPU_DUMP (ot + PsxGpu antes de dibujar + RAM de 2 MB): un fotograma
 * real del juego. La GPU por software lo dibuja anotando las primitivas (psx_gpu_record); GLES
 * (EGL pbuffer, Mesa) lo vuelve a dibujar a escala 1 y se compara con el area de dibujo del
 * software: pixeles identicos, pixeles casi iguales (diferencia <= 8 por canal) y PSNR. Con
 * salida, ademas, salida_sw.ppm (software), salida_x1.ppm y salida_x4.ppm (GLES a escala 4). */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "psx_mem.h"
#include "../gl/psx_hw.h"

static int x5(int c) { return (c << 3) | (c >> 2); }

static void ppm(const char *path, const uint32_t *rgba, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(&rgba[i], 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s <volcado.gpu> [salida]\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { puts("no se abre el volcado"); return 1; }
    static PsxGpu g;
    uint32_t ot;
    if (fread(&ot, 4, 1, f) != 1 || fread(&g, sizeof g, 1, f) != 1 || fread(psx_ram, 1, 0x200000, f) != 0x200000) {
        puts("volcado incompleto"); return 1;
    }
    fclose(f);
    static uint16_t bg[1024 * 512];
    memcpy(bg, g.vram, sizeof bg);
    int x0 = g.area_x1, y0 = g.area_y1, w = g.area_x2 - g.area_x1 + 1, h = g.area_y2 - g.area_y1 + 1;
    static PsxHwPrim prims[65536];
    static uint16_t data[1024 * 512];
    PsxHwList list = {.p = prims, .max = 65536, .data = data, .maxdata = 1024 * 512,
                      .main_x1 = g.area_x1, .main_y1 = g.area_y1, .main_x2 = g.area_x2, .main_y2 = g.area_y2};
    psx_gpu_record(&list, 0);
    psx_gpu_draw_list(&g, ot);
    psx_gpu_record_flush(&g);
    psx_gpu_record(NULL, 0);
    int kinds[6] = {0};
    for (int i = 0; i < list.n; i++) kinds[prims[i].kind]++;
    printf("%d primitivas anotadas (triangulos %d, lineas %d, rectangulos %d, rellenos %d, VRAM %d, subidas %d: %d px); area %dx%d en (%d,%d)\n",
           list.n, kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], list.ndata, w, h, x0, y0);

    if (getenv("HW_VRAMLIST"))
        for (int i = 0; i < list.n; i++)
            if (prims[i].kind == PSX_HW_VRAM) printf("  VRAM prim %d: %d,%d %dx%d\n", i, prims[i].x[0], prims[i].y[0], prims[i].x[1], prims[i].y[1]);
    if (getenv("HW_PROBE")) {   /* depuracion: primitivas que cubren el pixel x,y del area */
        int qx, qy;
        sscanf(getenv("HW_PROBE"), "%d,%d", &qx, &qy);
        qx += x0; qy += y0;
        for (int i = 0; i < list.n; i++) {
            PsxHwPrim *q = &prims[i];
            int in = 0;
            if (q->kind == PSX_HW_TRI) {
                long d0 = (long)(q->x[1] - q->x[0]) * (qy - q->y[0]) - (long)(q->y[1] - q->y[0]) * (qx - q->x[0]);
                long d1 = (long)(q->x[2] - q->x[1]) * (qy - q->y[1]) - (long)(q->y[2] - q->y[1]) * (qx - q->x[1]);
                long d2 = (long)(q->x[0] - q->x[2]) * (qy - q->y[2]) - (long)(q->y[0] - q->y[2]) * (qx - q->x[2]);
                in = (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
            } else if (q->kind == PSX_HW_RECT || q->kind == PSX_HW_FILL || q->kind == PSX_HW_VRAM)
                in = qx >= q->x[0] && qx < q->x[0] + q->x[1] && qy >= q->y[0] && qy < q->y[0] + q->y[1];
            if (in)
                printf("  prim %d tipo %d banderas %#x abr %d prof %d color %d,%d,%d pag %d,%d clut %d,%d area %d,%d-%d,%d\n", i, q->kind,
                       q->flags, q->abr, q->depth, q->r[0], q->g[0], q->b[0], q->tp_x, q->tp_y, q->clut_x, q->clut_y, q->ax1, q->ay1,
                       q->ax2, q->ay2);
        }
    }
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
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { puts("sin contexto GLES"); return 1; }
    if (psx_hw_init()) return 1;

    static uint32_t sw[1024 * 512], hw[4096 * 2048];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t c = g.vram[(y0 + y) * 1024 + x0 + x];
            sw[y * w + x] = (uint32_t)x5(c & 31) | (uint32_t)x5((c >> 5) & 31) << 8 | (uint32_t)x5((c >> 10) & 31) << 16 | 0xFF000000u;
        }
    psx_hw_dither(getenv("HW_SIN_TRAMADO") == NULL);   /* a escala 1, con el tramado del software */
    psx_hw_frame(&list, bg, x0, y0, w, h, 1);
    psx_hw_read(hw);
    psx_hw_dither(0);
    long same = 0, close = 0;
    double err = 0;
    for (int i = 0; i < w * h; i++) {
        int dmax = 0;
        for (int k = 0; k < 24; k += 8) {
            int d = (int)((sw[i] >> k) & 255) - (int)((hw[i] >> k) & 255);
            err += (double)d * d;
            if (abs(d) > dmax) dmax = abs(d);
        }
        same += dmax == 0;
        close += dmax <= 8;
    }
    err /= 3.0 * w * h;
    printf("escala 1 frente al software: %.2f %% pixeles identicos, %.2f %% casi iguales (<= 8), PSNR %.2f dB\n",
           100.0 * same / (w * h), 100.0 * close / (w * h), err > 0 ? 10 * log10(255.0 * 255.0 / err) : 99.0);
    if (argc > 2) {
        char p[600];
        snprintf(p, sizeof p, "%s_sw.ppm", argv[2]); ppm(p, sw, w, h);
        snprintf(p, sizeof p, "%s_x1.ppm", argv[2]); ppm(p, hw, w, h);
        psx_hw_frame(&list, bg, x0, y0, w, h, 4);
        psx_hw_read(hw);
        snprintf(p, sizeof p, "%s_x4.ppm", argv[2]); ppm(p, hw, w * 4, h * 4);
    }
    psx_hw_shutdown();
    return 0;
}
