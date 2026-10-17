/* Pruebas de la salida de video (psx/src/display.c y psx/gl/psx_present.c).
 *   psx/build/test_display [salida.ppm]
 * 1. Conversion de la VRAM visible: 15 y 24 bits, con el area en distintas posiciones y
 *    anchos; lo esperado se calcula desde los datos de ORIGEN (no se lee la VRAM).
 * 2. Pantalla apagada (SetDispMask(0)): negro.
 * 3. OpenGL ES 3 (EGL pbuffer, Mesa): la imagen presentada 1:1 y a escala entera x2, leida
 *    con glReadPixels, es identica pixel a pixel a la convertida; en 4:3 quedan barras. */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_display.h"
#include "psx_libgpu.h"
#include "psx_mem.h"
#include "../gl/psx_present.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t px(int r, int g, int b) { return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | 0xFF000000u; }
static int x5(int c) { return (c << 3) | (c >> 2); }
static uint32_t frame[1024 * 512], frame2[1024 * 512], gl[1280 * 1024];

static void put_disp(int x, int y, int w, int h, int rgb24) {
    DISPENV d;
    memset(&d, 0, sizeof d);
    d.disp.x = (int16_t)x; d.disp.y = (int16_t)y; d.disp.w = (int16_t)w; d.disp.h = (int16_t)h;
    d.isrgb24 = (uint8_t)rgb24;
    PutDispEnv(&d);
}

static void test_15(int x0, int y0, int w, int h) {
    static uint16_t src[512 * 512];
    for (int i = 0; i < w * h; i++) src[i] = (uint16_t)(i * 2654435761u >> 9);
    RECT r = {(int16_t)x0, (int16_t)y0, (int16_t)w, (int16_t)h};
    memcpy(psx_ram + 0x100000, src, (size_t)w * h * 2);
    LoadImage(&r, (uint32_t *)(psx_ram + 0x100000));
    put_disp(x0, y0, w, h, 0);
    int fw, fh;
    psx_display_frame(frame, 1024 * 512, &fw, &fh);
    int bad = 0;
    for (int i = 0; i < w * h; i++) {
        uint16_t c = src[i];
        if (frame[i] != px(x5(c & 31), x5((c >> 5) & 31), x5((c >> 10) & 31))) bad++;
    }
    CHECK(fw == w && fh == h && !bad, "15 bits en (%d,%d) %dx%d: %d pixeles distintos", x0, y0, w, h, bad);
}

static void test_24(int x0, int y0, int w, int h) {
    static uint8_t rgb[640 * 480 * 3];
    for (int i = 0; i < w * h * 3; i++) rgb[i] = (uint8_t)(i * 7 + (i >> 5));
    /* en la VRAM: 3 bytes por pixel, w*3/2 medias palabras por fila */
    int hw = (w * 3 + 1) / 2;
    RECT r = {(int16_t)x0, (int16_t)y0, (int16_t)hw, (int16_t)h};
    uint8_t *dst = psx_ram + 0x100000;
    memset(dst, 0, (size_t)hw * 2 * h);
    for (int y = 0; y < h; y++) memcpy(dst + (size_t)y * hw * 2, rgb + (size_t)y * w * 3, (size_t)w * 3);
    LoadImage(&r, (uint32_t *)dst);
    put_disp(x0, y0, w, h, 1);
    int fw, fh;
    psx_display_frame(frame, 1024 * 512, &fw, &fh);
    int bad = 0;
    for (int i = 0; i < w * h; i++)
        if (frame[i] != px(rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2])) bad++;
    CHECK(fw == w && fh == h && !bad, "24 bits en (%d,%d) %dx%d: %d pixeles distintos", x0, y0, w, h, bad);
}

static int gl_init(int W, int H, EGLDisplay *dpy_out, EGLSurface *surf_out, EGLContext *ctx_out) {
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) return -1;
    const EGLint attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    const EGLint pb[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
    const EGLint cattr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig cfg;
    EGLint n;
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglChooseConfig(dpy, attr, &cfg, 1, &n) || n < 1) return -1;
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattr);
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) return -1;
    *dpy_out = dpy; *surf_out = surf; *ctx_out = ctx;
    return 0;
}

/* lee el framebuffer (W x H) y lo da con la fila 0 arriba */
static void read_back(int W, int H) {
    static uint32_t tmp[1280 * 1024];
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    for (int y = 0; y < H; y++) memcpy(gl + y * W, tmp + (H - 1 - y) * W, (size_t)W * 4);
}

int main(int argc, char **argv) {
    ResetGraph(0);
    SetDispMask(1);
    test_15(0, 0, 320, 240);
    test_15(640, 256, 368, 240);
    test_15(1013, 500, 320, 40);   /* el area da la vuelta por la derecha y por abajo */
    test_24(0, 0, 320, 240);
    test_24(320, 16, 304, 224);    /* la pelicula 0 */
    test_24(3, 0, 640, 480);       /* x impar, 640 de ancho (960 medias palabras) */
    SetDispMask(0);
    put_disp(0, 0, 320, 240, 0);
    int fw, fh, on = psx_display_frame(frame, 1024 * 512, &fw, &fh), black = 1;
    for (int i = 0; i < fw * fh; i++) black &= frame[i] == 0xFF000000u;
    CHECK(!on && black, "pantalla apagada: negro");
    SetDispMask(1);

    /* GL: imagen de 15 bits de 320x240 y de 24 bits de 304x224 */
    EGLDisplay dpy; EGLSurface surf; EGLContext ctx;
    if (gl_init(1280, 960, &dpy, &surf, &ctx) || psx_present_init()) {
        printf("sin EGL/GLES 3: se omiten las pruebas de OpenGL\n");
    } else {
        for (int k = 0; k < 2; k++) {
            if (k == 0) test_15(0, 0, 320, 240); else test_24(320, 16, 304, 224);
            psx_display_frame(frame, 1024 * 512, &fw, &fh);
            psx_present_frame(frame, fw, fh, 1280, 960, PSX_FIT_EXACT, 0);
            read_back(1280, 960);
            int bad = 0;
            for (int y = 0; y < fh; y++)
                for (int x = 0; x < fw; x++) bad += gl[y * 1280 + x] != frame[y * fw + x];
            CHECK(!bad, "GL 1:1 %dx%d: %d pixeles distintos", fw, fh, bad);
            psx_present_frame(frame, fw, fh, 1280, 960, PSX_FIT_INTEGER, 0);
            read_back(1280, 960);
            int kx = 1280 / fw < 960 / fh ? 1280 / fw : 960 / fh, ox = (1280 - fw * kx) / 2, oy = (960 - fh * kx) / 2;
            bad = 0;
            for (int y = 0; y < fh * kx; y++)
                for (int x = 0; x < fw * kx; x++) bad += gl[(oy + y) * 1280 + ox + x] != frame[(y / kx) * fw + x / kx];
            int bars = (ox == 0 && oy == 0) || (gl[0] == 0xFF000000u && gl[1279] == 0xFF000000u);   /* solo si sobra margen */
            CHECK(!bad && bars, "GL escala entera x%d %dx%d: %d pixeles distintos, barras %d", kx, fw, fh, bad, bars);
        }
        /* 4:3: 368x240 ocupa 1280x960 entero; 320x240 en 1280x800 deja barras laterales */
        test_15(640, 256, 368, 240);
        psx_display_frame(frame, 1024 * 512, &fw, &fh);
        psx_present_frame(frame, fw, fh, 1280, 800, PSX_FIT_43, 0);
        read_back(1280, 800);
        int left = gl[400 * 1280 + 10] == 0xFF000000u && gl[400 * 1280 + 1269] == 0xFF000000u;
        int inside = gl[400 * 1280 + 640] != 0xFF000000u;
        CHECK(left && inside, "GL 4:3 en 1280x800: barras laterales %d, imagen %d", left, inside);
        if (argc > 1) {   /* la imagen de 24 bits a escala entera, para verla */
            test_24(320, 16, 304, 224);
            psx_display_frame(frame2, 1024 * 512, &fw, &fh);
            FILE *f = fopen(argv[1], "wb");
            fprintf(f, "P6\n%d %d\n255\n", fw, fh);
            for (int i = 0; i < fw * fh; i++) fwrite(&frame2[i], 1, 3, f);
            fclose(f);
        }
        psx_present_shutdown();
        eglTerminate(dpy);
    }
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
