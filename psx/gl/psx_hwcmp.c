/* Comparador del dibujo por GLES frente a la GPU por software en partida real (etapa 6, paso 17).
 *
 * psx_hwcmp_start(dir) engancha DrawOTag (psx_draw_list_hook): cada lista de dibujo del juego la
 * dibuja la GPU por software anotando las primitivas (la referencia) y GLES la vuelve a dibujar a
 * escala 1, con tramado, desde la VRAM de antes de la lista. Se comparan las dos en el area de
 * dibujo por bloques de 8x8: un pixel es "malo" si algun canal difiere en mas de BAD_DIFF (los
 * redondeos de la mezcla y el tramado quedan muy por debajo) y ademas su color no esta en los 3x3
 * vecinos de la otra imagen (en un sentido o en el otro): un borde o una linea movidos un pixel
 * no cuentan (GL coloca los vertices con 1/256 de pixel y en los empates exactos puede caer al
 * lado), un hueco, un pico o un recorte por mascara si. Un bloque es "malo" con BAD_PIX o mas
 * pixeles malos.
 *
 * Con la interpolacion (psx_hwcmp_interp), tambien las imagenes intermedias: los hilos de dibujo
 * las rasterizan por software y GLES las dibuja desde la misma lista.
 *
 * Salida: una linea por lista con bloques malos (hwcmp: ...) y, para algunas, <dir>/c<VBlank>.ppm
 * (intermedias: c<VBlank>_<k>-<n>.ppm)
 * con software | GLES | mapa de bloques malos (rojo). Al salir, el resumen. Requiere un contexto
 * GLES 3 (EGL pbuffer; en el PC, Mesa). */
#include <EGL/egl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_gpu.h"
#include "psx_hw.h"
#include "psx_mem.h"
#include "psx_interp.h"

#define BAD_DIFF 40
#define BAD_PIX 8
#define MAX_SAVES 60

extern void (*psx_draw_list_hook)(PsxGpu *g, uint32_t ot);
int VSync(int);

static const char *g_dir;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static EGLDisplay g_dpy;
static EGLSurface g_surf;
static EGLContext g_ctx;
static long g_lists, g_bad_lists, g_bad_blocks, g_pixels, g_same, g_close;
static int g_saves, g_last_save = -1000;
/* listas con menos del 99 % de pixeles casi iguales (diferencias repartidas que los bloques no
 * ven: p. ej. el fondo leido un pixel al lado) y la peor */
static long g_low, g_inter;
static double g_worst = 101.0;
static int g_worst_vb;
static PsxHwPrim g_prims[65536];
static uint16_t g_data[1024 * 512], g_bg[1024 * 512];
static uint32_t g_sw[1024 * 512], g_hw[1024 * 512];

static int dmax(uint32_t a, uint32_t b) {
    int m = 0;
    for (int k = 0; k < 24; k += 8) {
        int d = abs((int)((a >> k) & 255) - (int)((b >> k) & 255));
        if (d > m) m = d;
    }
    return m;
}

/* el color c esta en los 3x3 vecinos de (x, y) de img */
static int near(const uint32_t *img, int w, int h, int x, int y, uint32_t c) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int X = x + dx, Y = y + dy;
            if (X >= 0 && Y >= 0 && X < w && Y < h && dmax(img[Y * w + X], c) <= BAD_DIFF) return 1;
        }
    return 0;
}

static uint32_t x5(uint16_t c) {
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16;
}

static void save(int vb, int num, int den, int w, int h, const uint8_t *badmap, int bw) {
    char path[600];
    if (num == den) snprintf(path, sizeof path, "%s/c%05d.ppm", g_dir, vb);
    else snprintf(path, sizeof path, "%s/c%05d_%d-%d.ppm", g_dir, vb, num, den);
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

static void compare(const PsxHwList *lp, const uint16_t *bg, const uint16_t *swv, int x0, int y0, int w, int h, int num, int den);
int psx_hwcmp_only;          /* lo pone native_main (AC3_HWCMP_ONLY) mientras hay mision */
static int g_cmp_persist;    /* compare: la GPU conserva la imagen (real) */

static void hook(PsxGpu *g, uint32_t ot) {
    int x0 = g->area_x1, y0 = g->area_y1, w = g->area_x2 - g->area_x1 + 1, h = g->area_y2 - g->area_y1 + 1;
    if (w <= 0 || h <= 0 || w > 1024 || h > 512) { psx_gpu_draw_list(g, ot); return; }
    int dv;   /* depuracion: AC3_HWCMP_DUMP=vblank,archivo: la lista para psx/test/test_hw (como PSX_GPU_DUMP) */
    char dpath[400];
    if (getenv("AC3_HWCMP_DUMP") && sscanf(getenv("AC3_HWCMP_DUMP"), "%d,%399s", &dv, dpath) == 2 && VSync(-1) == dv) {
        FILE *f = fopen(dpath, "wb");
        if (f) {
            fwrite(&ot, 4, 1, f);
            fwrite(g, sizeof *g, 1, f);
            fwrite(psx_ram, 1, PSX_RAM_SIZE, f);
            fclose(f);
        }
    }
    memcpy(g_bg, g->vram, sizeof g_bg);
    PsxHwList l = {.p = g_prims, .max = 65536, .data = g_data, .maxdata = 1024 * 512,
                   .main_x1 = g->area_x1, .main_y1 = g->area_y1, .main_x2 = g->area_x2, .main_y2 = g->area_y2};
    if (psx_hwcmp_only) {
        /* etapa 8, paso 2 (AC3_HWCMP_ONLY, en la mision): el software solo interpreta la lista y la
         * GPU parte de lo que conserva; la referencia la dibuja entera una copia del software, que
         * despues pasa a ser la VRAM (las zonas siguen marcadas como solo de la GPU) */
        static PsxGpu ref;
        memcpy(&ref, g, sizeof ref);
        l.main_stale = psx_gpu_main_stale(g->area_x1, g->area_y1, g->area_x2, g->area_y2);
        /* como en la app: la lista que copia desde la imagen la dibuja entera el software */
        psx_gpu_record(&l, !psx_gpu_list_copies_from(ot, psx_ram, psx_scratch, g->area_x1, g->area_y1, g->area_x2, g->area_y2));
        psx_gpu_draw_list(g, ot);
        psx_gpu_record_flush(g);
        psx_gpu_record(NULL, 0);
        psx_gpu_draw_list_mem(&ref, ot, psx_ram, psx_scratch);
        g_cmp_persist = 1;
        compare(&l, g_bg, ref.vram, x0, y0, w, h, 1, 1);
        g_cmp_persist = 0;
        memcpy(g, &ref, sizeof *g);
        return;
    }
    psx_gpu_record(&l, 0);
    psx_gpu_draw_list(g, ot);
    psx_gpu_record_flush(g);
    psx_gpu_record(NULL, 0);
    psx_gpu_record(NULL, 0);
    compare(&l, g_bg, g->vram, x0, y0, w, h, 1, 1);
}

/* sumidero de la interpolacion (AC3_HWCMP con AC3_INTERP y AC3_INTERP_THREADS): imagenes reales
 * e intermedias, con la del software (psx_interp_hw(..., 1)) */
static void interp_sink(const PsxHwList *l, const uint16_t *bg, const uint16_t *sw, int x0, int y0, int w, int h, int num,
                        int den, int vb, double t) {
    (void)vb; (void)t;
    if (sw) compare(l, bg, sw, x0, y0, w, h, num, den);
}

/* compara una lista: GLES (escala 1) desde bg frente a swv, la VRAM tras el software; area
 * (x0, y0, w, h). La llaman el enganche de DrawOTag y el sumidero de la interpolacion (hilo del
 * juego y de dibujo: va bajo un cerrojo, con el contexto tomado solo mientras tanto) */
static void compare(const PsxHwList *lp, const uint16_t *bg, const uint16_t *swv, int x0, int y0, int w, int h, int num, int den) {
    if (lp->n == 0 || w <= 0 || h <= 0 || w > 1024 || h > 512) return;
    pthread_mutex_lock(&g_mu);
    eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx);
    if (getenv("AC3_HWCMP_MASK")) {   /* depuracion: primitivas con bit de mascara (poner / comprobar) */
        int n[16] = {0};
        for (int i = 0; i < lp->n; i++) {
            int f = lp->p[i].flags;
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
        for (int i = 0; i < lp->n; i++) {
            const PsxHwPrim *q = &lp->p[i];
            int in = 0;
            if (q->kind == PSX_HW_TRI) {
                long d0 = (long)(q->x[1] - q->x[0]) * (Y - q->y[0]) - (long)(q->y[1] - q->y[0]) * (X - q->x[0]);
                long d1 = (long)(q->x[2] - q->x[1]) * (Y - q->y[1]) - (long)(q->y[2] - q->y[1]) * (X - q->x[1]);
                long d2 = (long)(q->x[0] - q->x[2]) * (Y - q->y[2]) - (long)(q->y[0] - q->y[2]) * (X - q->x[2]);
                in = (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
            } else if (q->kind == PSX_HW_RECT || q->kind == PSX_HW_FILL || q->kind == PSX_HW_VRAM)
                in = X >= q->x[0] && X < q->x[0] + q->x[1] && Y >= q->y[0] && Y < q->y[0] + q->y[1];
            if (in)
                printf("sonda: prim %d tipo %d banderas %#x abr %d prof %d pag %d,%d clut %d,%d ventana %d,%d,%d,%d color %d,%d,%d (%d,%d) (%d,%d) (%d,%d) uv (%d,%d) (%d,%d) (%d,%d) area %d,%d-%d,%d\n", i, q->kind, q->flags, q->abr, q->depth, q->tp_x, q->tp_y, q->clut_x, q->clut_y, q->tw_mx, q->tw_my, q->tw_ox, q->tw_oy, q->r[0], q->g[0], q->b[0],
                       q->x[0] - x0, q->y[0] - y0, q->x[1] - x0, q->y[1] - y0, q->x[2] - x0, q->y[2] - y0, q->u[0], q->v[0], q->u[1], q->v[1],
                       q->u[2], q->v[2], q->ax1 - x0, q->ay1 - y0, q->ax2 - x0, q->ay2 - y0);
        }
    }
    /* AC3_HWCMP_PERSIST=1 (etapa 8, paso 2): la GPU conserva cada imagen y parte de ella, no de la
     * VRAM del software (que aqui si se dibuja entera, de referencia) */
    static int persist = -1;
    if (persist < 0) persist = getenv("AC3_HWCMP_PERSIST") != NULL;
    if (g_cmp_persist) psx_hw_frame_ex(lp, bg, x0, y0, w, h, 1, 1);
    else if (persist) {
        PsxHwList l2 = *lp;
        l2.main_stale = 1;
        psx_hw_frame_ex(&l2, bg, x0, y0, w, h, 1, num == den);
    } else psx_hw_frame(lp, bg, x0, y0, w, h, 1);
    psx_hw_read(g_hw);
    if (g_cmp_persist && getenv("AC3_HWCMP_MASKCMP")) {   /* el bit 15 de la GPU frente al del software */
        static uint8_t mk[1024 * 512];
        if (!psx_hw_read_mask(mk)) {
            int n = 0, shown = 0;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int s15 = (swv[((y0 + y) & 511) * 1024 + ((x0 + x) & 1023)] >> 15) & 1;
                    if (s15 != mk[y * w + x]) { n++; if (shown++ < 4) printf("  mascara (%d,%d): software %d, GPU %d\n", x, y, s15, mk[y * w + x]); }
                }
            if (n) printf("mascara VBlank %d: %d pixeles con el bit 15 distinto\n", VSync(-1), n);
        }
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) g_sw[y * w + x] = x5(swv[((y0 + y) & 511) * 1024 + ((x0 + x) & 1023)]);
    int bw = (w + 7) / 8, bh = (h + 7) / 8, nbad = 0, bx1 = 9999, by1 = 9999, bx2 = -1, by2 = -1;
    static uint8_t badmap[128 * 64];
    static uint16_t cnt[128 * 64];
    memset(cnt, 0, sizeof cnt);
    long same = 0, close = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t a = g_sw[y * w + x], b = g_hw[y * w + x];
            int d = dmax(a, b);
            same += d == 0;
            close += d <= 8;
            if (d > BAD_DIFF && (!near(g_sw, w, h, x, y, b) || !near(g_hw, w, h, x, y, a))) cnt[(y / 8) * bw + x / 8]++;
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
    g_inter += num != den;
    g_pixels += (long)w * h; g_same += same; g_close += close;
    double pc = 100.0 * close / ((double)w * h);
    if (pc < 99.0) g_low++;
    if (pc < g_worst) { g_worst = pc; g_worst_vb = VSync(-1); }
    if (!nbad) goto done;
    g_bad_lists++;
    g_bad_blocks += nbad;
    int vb = VSync(-1);
    printf("hwcmp: VBlank %d, %s %d/%d, lista %dx%d en (%d,%d), %d primitivas: %d bloques malos en (%d,%d)-(%d,%d); identicos %.1f %%\n", vb,
           num == den ? "real" : "intermedia", num, den, w, h, x0, y0, lp->n, nbad, bx1 * 8, by1 * 8, bx2 * 8 + 7, by2 * 8 + 7, 100.0 * same / ((double)w * h));
    if (g_saves < MAX_SAVES && vb - g_last_save >= 30) {
        save(vb, num, den, w, h, badmap, bw);
        g_saves++;
        g_last_save = vb;
    }
done:
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    pthread_mutex_unlock(&g_mu);
}


static void summary(void) {
    printf("hwcmp: %ld listas comparadas, %ld con bloques malos (%ld bloques); pixeles identicos %.2f %%, casi iguales %.2f %%\n",
           g_lists, g_bad_lists, g_bad_blocks, g_pixels ? 100.0 * g_same / g_pixels : 0.0, g_pixels ? 100.0 * g_close / g_pixels : 0.0);
    printf("hwcmp: de ellas, %ld imagenes intermedias\n", g_inter);
    extern int psx_hw_stat_hoisted, psx_hw_stat_inframe, psx_hw_stat_atlas;
    printf("hwcmp: subidas al atlas %d, adelantadas %d, a mitad de imagen %d\n", psx_hw_stat_atlas, psx_hw_stat_hoisted, psx_hw_stat_inframe);
    printf("hwcmp: %ld listas por debajo del 99 %% de casi iguales; la peor, %.2f %% (VBlank %d)\n", g_low, g_lists ? g_worst : 100.0,
           g_worst_vb);
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
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    psx_draw_list_hook = hook;
    atexit(summary);
    return 0;
}

/* con la interpolacion: las imagenes llegan por su sumidero (real e intermedias) */
void psx_hwcmp_interp(void) { psx_interp_hw(interp_sink, 1); }
