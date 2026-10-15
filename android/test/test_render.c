/* Prueba del render en el PC: EGL pbuffer + GLES3 (Mesa), mismo render.c que el APK.
 * Guarda un fotograma en crudo (RGBA) para convertirlo a PNG.
 *   build/test_render <imagen.bin> <mision> <avion> <segundos> <salida.rgba>
 */
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include "app.h"

int main(int argc, char **argv) {
    if (argc < 6) return 2;
    const int W = 960, H = 540;
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) { puts("sin EGL"); return 1; }
    const EGLint attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    const EGLint pb[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
    const EGLint cattr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig cfg; EGLint n;
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglChooseConfig(dpy, attr, &cfg, 1, &n) || n < 1) { puts("sin config ES3"); return 1; }
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattr);
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { puts("sin contexto"); return 1; }
    printf("GL: %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    static App a;
    a.w = W; a.h = H;
    a.scene = calloc(1, sizeof(Scene));
    scene_init(a.scene);
    if (disc_open(&a.disc, argv[1]) || pack_open(&a.disc, &a.pack)) { puts("disco"); return 1; }
    a.mission = atoi(argv[2]); a.plane = atoi(argv[3]);
    if (scene_load_mission(a.scene, &a.disc, &a.pack, a.mission) ||
        scene_load_aircraft(a.scene, &a.disc, &a.pack, a.plane)) { puts("carga"); return 1; }
    gl_init(&a);
    gl_upload(&a);
    a.status = 1;
    reset_flight(&a);
    /* vuelo de prueba: un poco de alabeo y cabeceo para ver el avion inclinado */
    float secs = atof(argv[4]);
    for (float t = 0; t < secs; t += 1.0f / 60) {
        if (argc > 6) { a.lx = t < secs * 0.5f ? 0.3f : 0.0f; a.ly = 0.35f; }
        update(&a, 1.0f / 60);
    }
    draw(&a);
    glFinish();
    GLenum err = glGetError();
    unsigned char *px = malloc(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(argv[5], "wb");
    for (int y = H - 1; y >= 0; y--) fwrite(px + y * W * 4, 1, W * 4, f);  /* volteo vertical */
    fclose(f);
    printf("glGetError=%#x  pos=(%.0f, %.0f, %.0f) suelo=%.0f velocidad=%.0f\n", err, a.pos.x, a.pos.y, a.pos.z,
           scene_ground(a.scene, a.pos.x, a.pos.z), a.speed);
    return 0;
}
