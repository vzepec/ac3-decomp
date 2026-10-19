/* Comparador del dibujo por GLES frente a la GPU por software en partida real (etapa 6, paso 17).
 *
 * psx_hwcmp_start(dir) engancha DrawOTag (psx_draw_list_hook): cada lista de dibujo del juego la
 * dibuja la GPU por software anotando las primitivas (la referencia) y GLES la vuelve a dibujar a
 * escala 1, con tramado, desde la VRAM de antes de la lista. Se comparan las dos en el area de
 * dibujo por bloques de 8x8: un pixel es "malo" si algun canal difiere en mas de BAD_DIFF (los
 * redondeos de la mezcla y el tramado quedan muy por debajo) y un bloque es "malo" con BAD_PIX o
 * mas pixeles malos. Asi salen los fallos que se ven (poligonos estirados, huecos, recortes por
 * mascara) y no las diferencias de redondeo.
 *
 * Salida: una linea por lista con bloques malos (hwcmp: ...) y, para algunas, <dir>/c<VBlank>.ppm
 * con software | GLES | mapa de bloques malos (rojo). Al salir, el resumen. Requiere un contexto
 * GLES 3 (EGL pbuffer; en el PC, Mesa). */
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "psx_hw.h"

#define BAD_DIFF 40
#define BAD_PIX 8
#define MAX_SAVES 60

extern void (*psx_draw_list_hook)(PsxGpu *g, uint32_t ot);
int VSync(int);

static const char *g_dir;
static EGLDisplay g_dpy;
static EGLSurface g_surf;
static EGLContext g_ctx;
static long g_lists, g_bad_lists, g_bad_blocks, g_pixels, g_same, g_close;
static int g_saves, g_last_save = -1000;
static PsxHwPrim g_prims[65536];
static uint16_t g_data[1024 * 512], g_bg[1024 * 512];
static uint32_t g_sw[1024 * 512], g_hw[1024 * 512];

static uint32_t x5(uint16_t c) {
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16;
}

static void save(int vb, int w, int h, const uint8_t *badmap, int bw) {
    char path[600];
    snprintf(path, sizeof path, "%s/c%05d.ppm", g_dir, vb);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w * 3 + 8, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) fwrite(&g_sw[y * w + x], 1, 3, f);
        for (int x = 0; x < 4; x++) fwrite("\0\0\0", 1, 3, f);
        for (int x = 0; x < w; x++) fwrite(&g_hw[y * w + x], 1, 3, f);
        for (int x = 0; x < 4; x++) fwrite("\0\0\0", 1, 3, f);
        for (int x = 0; x < w; x++) {
            uint32_t c = g_sw[y * w + x];
            uint8_t p[3] = {(uint8_t)((c & 255) / 3), (uint8_t)(((c >> 8) & 255) / 3), (uint8_t)(((c >> 16) & 255) / 3)};
            if (badmap[(y / 8) * bw + x / 8]) p[0] = 255;
            fwrite(p, 1, 3, f);
        }
    }
    fclose(f);
}

static void hook(PsxGpu *g, uint32_t ot) {
    int x0 = g->area_x1, y0 = g->area_y1, w = g->area_x2 - g->area_x1 + 1, h = g->area_y2 - g->area_y1 + 1;
    if (w <= 0 || h <= 0 || w > 1024 || h > 512) { psx_gpu_draw_list(g, ot); return; }
    memcpy(g_bg, g->vram, sizeof g_bg);
    PsxHwList l = {.p = g_prims, .max = 65536, .data = g_data, .maxdata = 1024 * 512,
                   .main_x1 = g->area_x1, .main_y1 = g->area_y1, .main_x2 = g->area_x2, .main_y2 = g->area_y2};
    psx_gpu_record(&l, 0);
    psx_gpu_draw_list(g, ot);
    psx_gpu_record_flush(g);
    psx_gpu_record(NULL, 0);
    if (l.n == 0) return;
    if (getenv("AC3_HWCMP_MASK")) {   /* depuracion: primitivas con bit de mascara (poner / comprobar) */
        int n[16] = {0};
        for (int i = 0; i < l.n; i++) {
            int f = g_prims[i].flags;
            if (f & (PSX_HWF_MASKSET | PSX_HWF_MASKCHK))
                n[(f & PSX_HWF_MASKSET ? 1 : 0) | (f & PSX_HWF_MASKCHK ? 2 : 0) | (f & PSX_HWF_SEMI ? 4 : 0) | (f & PSX_HWF_TEX ? 8 : 0)]++;
        }
        printf("mascara VBlank %d:", VSync(-1));
        for (int k = 0; k < 16; k++) if (n[k]) printf(" [%s%s%s%s] %d", k & 1 ? "pone" : "", k & 2 ? " comprueba" : "", k & 4 ? " semi" : "", k & 8 ? " tex" : "", n[k]);
        printf("\n");
    }
    int pv, px, py;   /* depuracion: AC3_HWCMP_PROBE=vblank,x,y: primitivas que cubren ese pixel del area */
    if (getenv("AC3_HWCMP_PROBE") && sscanf(getenv("AC3_HWCMP_PROBE"), "%d,%d,%d", &pv, &px, &py) == 3 && VSync(-1) == pv) {
        int X = px + x0, Y = py + y0;
        for (int i = 0; i < l.n; i++) {
            const PsxHwPrim *q = &g_prims[i];
            int in = 0;
            if (q->kind == PSX_HW_TRI) {
                long d0 = (long)(q->x[1] - q->x[0]) * (Y - q->y[0]) - (long)(q->y[1] - q->y[0]) * (X - q->x[0]);
                long d1 = (long)(q->x[2] - q->x[1]) * (Y - q->y[1]) - (long)(q->y[2] - q->y[1]) * (X - q->x[1]);
                long d2 = (long)(q->x[0] - q->x[2]) * (Y - q->y[2]) - (long)(q->y[0] - q->y[2]) * (X - q->x[2]);
                in = (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
            } else if (q->kind == PSX_HW_RECT || q->kind == PSX_HW_FILL || q->kind == PSX_HW_VRAM)
                in = X >= q->x[0] && X < q->x[0] + q->x[1] && Y >= q->y[0] && Y < q->y[0] + q->y[1];
            if (in)
                printf("sonda: prim %d tipo %d banderas %#x (%d,%d) (%d,%d) (%d,%d) uv (%d,%d) (%d,%d) (%d,%d) area %d,%d-%d,%d\n", i, q->kind, q->flags,
                       q->x[0] - x0, q->y[0] - y0, q->x[1] - x0, q->y[1] - y0, q->x[2] - x0, q->y[2] - y0, q->u[0], q->v[0], q->u[1], q->v[1],
                       q->u[2], q->v[2], q->ax1 - x0, q->ay1 - y0, q->ax2 - x0, q->ay2 - y0);
        }
    }
    eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx);
    psx_hw_frame(&l, g_bg, x0, y0, w, h, 1);
    psx_hw_read(g_hw);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) g_sw[y * w + x] = x5(g->vram[((y0 + y) & 511) * 1024 + ((x0 + x) & 1023)]);
    int bw = (w + 7) / 8, bh = (h + 7) / 8, nbad = 0, bx1 = 9999, by1 = 9999, bx2 = -1, by2 = -1;
    static uint8_t badmap[128 * 64];
    static uint16_t cnt[128 * 64];
    memset(cnt, 0, sizeof cnt);
    long same = 0, close = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t a = g_sw[y * w + x], b = g_hw[y * w + x];
            int dmax = 0;
            for (int k = 0; k < 24; k += 8) {
                int d = abs((int)((a >> k) & 255) - (int)((b >> k) & 255));
                if (d > dmax) dmax = d;
            }
            same += dmax == 0;
            close += dmax <= 8;
            if (dmax > BAD_DIFF) cnt[(y / 8) * bw + x / 8]++;
        }
    for (int i = 0; i < bw * bh; i++) {
        badmap[i] = cnt[i] >= BAD_PIX;
        if (!badmap[i]) continue;
        nbad++;
        int bx = i % bw, by = i / bw;
        if (bx < bx1) bx1 = bx;
        if (by < by1) by1 = by;
        if (bx > bx2) bx2 = bx;
        if (by > by2) by2 = by;
    }
    g_lists++;
    g_pixels += (long)w * h; g_same += same; g_close += close;
    if (!nbad) return;
    g_bad_lists++;
    g_bad_blocks += nbad;
    int vb = VSync(-1);
    printf("hwcmp: VBlank %d, lista %dx%d en (%d,%d), %d primitivas: %d bloques malos en (%d,%d)-(%d,%d); identicos %.1f %%\n", vb, w, h,
           x0, y0, l.n, nbad, bx1 * 8, by1 * 8, bx2 * 8 + 7, by2 * 8 + 7, 100.0 * same / ((double)w * h));
    if (g_saves < MAX_SAVES && vb - g_last_save >= 30) {
        save(vb, w, h, badmap, bw);
        g_saves++;
        g_last_save = vb;
    }
}

static void summary(void) {
    printf("hwcmp: %ld listas comparadas, %ld con bloques malos (%ld bloques); pixeles identicos %.2f %%, casi iguales %.2f %%\n",
           g_lists, g_bad_lists, g_bad_blocks, g_pixels ? 100.0 * g_same / g_pixels : 0.0, g_pixels ? 100.0 * g_close / g_pixels : 0.0);
}

int psx_hwcmp_start(const char *dir) {
    g_dir = dir;
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(g_dpy, NULL, NULL);
    const EGLint attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8,
                           EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint nc;
    eglChooseConfig(g_dpy, attr, &cfg, 1, &nc);
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    g_surf = eglCreatePbufferSurface(g_dpy, cfg, pb);
    const EGLint cattr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    eglBindAPI(EGL_OPENGL_ES_API);
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, cattr);
    if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx) || psx_hw_init()) return -1;
    psx_hw_dither(1);
    psx_draw_list_hook = hook;
    atexit(summary);
    return 0;
}
