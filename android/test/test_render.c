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
    scene_load_common(a.scene, &a.disc, &a.pack);
    if (scene_load_mission(a.scene, &a.disc, &a.pack, a.mission) ||
        scene_load_aircraft(a.scene, &a.disc, &a.pack, a.plane)) { puts("carga"); return 1; }
    gl_init(&a);
    gl_upload(&a);
    a.status = 1;
    reset_flight(&a);
    /* vuelo de prueba: un poco de alabeo y cabeceo para ver el avion inclinado */
    float secs = atof(argv[4]);
    if (argc > 9) {  /* picado: 300 por encima del suelo, morro 30 grados abajo, sin mandos */
        a.pos.y = scene_ground(a.scene, a.pos.x, a.pos.z) + 300;
        a.fwd = (V3){a.fwd.x * 0.866f, -0.5f, a.fwd.z * 0.866f};
        a.up = (V3){a.right.y * a.fwd.z - a.right.z * a.fwd.y, a.right.z * a.fwd.x - a.right.x * a.fwd.z,
                    a.right.x * a.fwd.y - a.right.y * a.fwd.x};
        for (float t = 0; t < secs; t += 1.0f / 60) {
            float y0 = a.pos.y;
            update(&a, 1.0f / 60);
            if (a.crash_t > 0 && a.crashes == 1) { printf("estrellado en t=%.2f s a y=%.0f (suelo %.0f)\n", t, y0, scene_ground(a.scene, a.pos.x, a.pos.z)); a.crashes = 2; }
        }
        printf("choques %d, crash_t %.2f, pos y %.0f\n", a.crashes > 1 ? 1 : a.crashes, a.crash_t, a.pos.y);
        return 0;
    }
    for (float t = 0; t < secs; t += 1.0f / 60) {
        if (argc > 6) { a.lx = t < secs * 0.5f ? 0.3f : 0.0f; a.ly = 0.35f; }
        a.fire = ((int)(t * 4) % 2) == 0;   /* dispara a rafagas */
        update(&a, 1.0f / 60);
    }
    a.fps_shown = 60;
    if (argc > 7) {  /* aviones del guion delante para verlos de cerca, y una explosion */
        int k = 0;
        for (int i = 1; i < MAX_ENEMIES && k < 3; i++) {
            Enemy *e = &a.enemies[i];
            if (e->mesh < 0 || !a.scene->models[e->mesh].n) continue;
            e->alive = 1;
            e->pos = (V3){a.pos.x + a.fwd.x * (380 + 260 * k) + a.right.x * (k - 1) * 140, a.pos.y + 20 + 25 * k, a.pos.z + a.fwd.z * (380 + 260 * k)};
            e->fwd = (V3){-a.right.x, 0, -a.right.z}; e->up = (V3){0, 1, 0}; e->right = a.fwd; e->bank = 0;
            k++;
        }
        extern void test_boom(App *a, V3 p);
        test_boom(&a, (V3){a.pos.x + a.fwd.x * 600 + a.right.x * 200, a.pos.y + a.fwd.y * 600, a.pos.z + a.fwd.z * 600 + a.right.z * 200});
        for (int f = 0; f < atoi(argv[7]); f++) update(&a, 1.0f / 60);
    }
    if (argc > 8) {  /* camara junto al objeto de tierra N del guion */
        const Spawn *o = &a.scene->obj[atoi(argv[8])];
        a.pos = (V3){o->x - 500, o->y + 160, o->z + 300};
        V3 f = {o->x - a.pos.x, o->y - a.pos.y, o->z - a.pos.z};
        float l = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
        a.fwd = (V3){f.x / l, f.y / l, f.z / l};
        a.right = (V3){-a.fwd.z, 0, a.fwd.x};
        float rl = sqrtf(a.right.x * a.right.x + a.right.z * a.right.z);
        a.right.x /= rl; a.right.z /= rl;
        a.up = (V3){a.right.y * a.fwd.z - a.right.z * a.fwd.y, a.right.z * a.fwd.x - a.right.x * a.fwd.z,
                    a.right.x * a.fwd.y - a.right.y * a.fwd.x};
        printf("objeto %d: modelo %d malla %d en (%.0f, %.0f, %.0f)\n", atoi(argv[8]), o->model, o->mesh, o->x, o->y, o->z);
    }
    draw(&a);
    int vivos = 0, part = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) vivos += a.enemies[i].alive;
    for (int i = 0; i < MAX_PARTICLES; i++) part += a.parts[i].active;
    printf("enemigos vivos: %d  blanco: %d  derribos: %d  particulas: %d\n", vivos, a.target, a.kills, part);
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
