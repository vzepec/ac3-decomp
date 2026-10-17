/* AC3 Slice: rebanada vertical de la etapa 4 (docs/05-hoja-de-ruta.md).
 *
 * Un avion del jugador volando sobre el terreno de una mision, con el mando
 * fisico de la Retroid. Todo se lee de la imagen de disco del usuario: el APK
 * no contiene nada del juego.
 *
 * Render: VRAM del PS1 simulada como textura R16UI (1024x512) y consulta de
 * CLUT/tpage en el fragment shader, de modo que las texturas salen igual que
 * en la consola. Sin tope de fps: vsync a la frecuencia nativa de la pantalla
 * y fisica por delta-time.
 *
 * Colores de pantalla de estado (no hay texto todavia):
 *   azul oscuro  = esperando la imagen de disco (selector de archivos) o cargando
 *   rojo         = fallo al cargar una mision o un avion (ver logcat)
 */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "music.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "AC3Slice", __VA_ARGS__)



typedef struct {
    struct android_app *app;
    EGLDisplay dpy;
    EGLSurface surf;
    EGLContext ctx;
} Platform;

static Platform P;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ disco */

/* Imagen de disco: la elige el usuario con el selector de archivos del sistema
 * (MainActivity.java), que entrega aqui un descriptor abierto. Sin permisos. */
static volatile int g_disc_fd = -1;

JNIEXPORT void JNICALL Java_com_vzepec_ac3slice_MainActivity_nativeSetDisc(JNIEnv *env, jclass cls, jint fd) {
    (void)env; (void)cls;
    int old = __atomic_exchange_n(&g_disc_fd, fd, __ATOMIC_SEQ_CST);
    if (old >= 0) close(old);  /* uno anterior que no llego a usarse */
}

/* Pide a la actividad Java que abra el selector de archivos. */
static void request_picker(void) {
    ANativeActivity *act = P.app->activity;
    JNIEnv *env;
    if ((*act->vm)->AttachCurrentThread(act->vm, &env, NULL) != JNI_OK) return;
    jclass c = (*env)->GetObjectClass(env, act->clazz);
    jmethodID m = (*env)->GetMethodID(env, c, "pickDisc", "()V");
    if (m) (*env)->CallVoidMethod(env, act->clazz, m);
    (*env)->DeleteLocalRef(env, c);
    (*act->vm)->DetachCurrentThread(act->vm);
}

/* Si ha llegado un descriptor nuevo, abre esa imagen y la valida. */
static int take_disc(App *a) {
    int fd = __atomic_exchange_n(&g_disc_fd, -1, __ATOMIC_SEQ_CST);
    if (fd < 0) return 0;
    Disc d;
    if (disc_open_fd(&d, fd)) { close(fd); LOG("no se puede leer el archivo elegido"); request_picker(); return 0; }
    if (!disc_volume_is_ac3(&d)) {
        disc_close(&d);
        LOG("el archivo elegido no es una imagen .bin de Ace Combat 3");
        request_picker();
        return 0;
    }
    AcePack p;
    if (pack_open(&d, &p)) { disc_close(&d); LOG("no se puede abrir ACE.BPB"); request_picker(); return 0; }
    if (a->disc.f) { music_stop(); a->music_entry = -1; pack_free(&a->pack); disc_close(&a->disc); }
    a->disc = d;
    a->pack = p;
    a->reload_mission = 1;
    LOG("imagen de disco valida");
    return 1;
}

/* ------------------------------------------------------------------ carga */

static void load_data(App *a) {
    if (!a->scene) {
        a->scene = calloc(1, sizeof(Scene));
        scene_init(a->scene);
    }
    double t0 = now();
    if (a->reload_mission) {
        Scene *s = a->scene;
        audio_stop_all();   /* antes de liberar: el mezclador lee las muestras de la escena */
        scene_free(s);   /* libera todas las mallas (antes quedaban sin liberar las de enemigos) */
        scene_init(s);
        if (scene_load_common(s, &a->disc, &a->pack)) LOG("sin texturas de efectos (registro 0000)");
        int r = scene_load_mission(s, &a->disc, &a->pack, a->mission);
        if (r) {  /* no reintentar en cada fotograma: esperar a que se elija otra mision o avion */
            a->status = -2; a->reload_mission = a->reload_plane = 0;
            LOG("fallo cargando la mision %u: %d", a->mission, r);
            return;
        }
        a->reload_plane = 1;
        a->reload_mission = 0;
        reset_flight(a);
    }
    if (a->reload_plane) {
        int r = scene_load_aircraft(a->scene, &a->disc, &a->pack, a->plane);
        if (r) { a->status = -2; a->reload_plane = 0; LOG("fallo cargando el avion %u: %d", a->plane, r); return; }
        a->reload_plane = 0;
    }
    LOG("mision %u, avion %u: %u triangulos de terreno, %u de avion, %d TIM, %d aviones y %d objetos, %.0f ms",
        a->mission, a->plane, a->scene->tiles.n / 3, a->scene->plane.n / 3, a->scene->tims_loaded, a->scene->nair,
        a->scene->nobj, (now() - t0) * 1000);
    a->status = 1;
    gl_upload(a);
    if (audio_init(a->scene->sounds)) LOG("sin salida de audio");
    if (a->music_entry != music_track_for_mission(a->mission)) {
        a->music_entry = music_track_for_mission(a->mission);
        if (music_start(&a->disc, a->music_entry)) LOG("sin musica (hace falta una imagen .bin de 2352 bytes)");
    }
}

/* ------------------------------------------------------------------ EGL */

static int egl_init(App *a) {
    const EGLint attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    const EGLint cattr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig cfg;
    EGLint n;
    P.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(P.dpy, NULL, NULL);
    if (!eglChooseConfig(P.dpy, attr, &cfg, 1, &n) || n < 1) return -1;
    P.surf = eglCreateWindowSurface(P.dpy, cfg, P.app->window, NULL);
    P.ctx = eglCreateContext(P.dpy, cfg, EGL_NO_CONTEXT, cattr);
    if (!eglMakeCurrent(P.dpy, P.surf, P.surf, P.ctx)) return -1;
    eglSwapInterval(P.dpy, 1);  /* vsync a la frecuencia nativa (60 Hz en la Flip 2), sin tope de 30 */
    eglQuerySurface(P.dpy, P.surf, EGL_WIDTH, &a->w);
    eglQuerySurface(P.dpy, P.surf, EGL_HEIGHT, &a->h);
    LOG("EGL %dx%d, GL %s", a->w, a->h, glGetString(GL_VERSION));
    gl_init(a);
    if (a->status == 1) gl_upload(a);
    return 0;
}

static void egl_term(App *a) {
    if (P.dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(P.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (P.ctx != EGL_NO_CONTEXT) eglDestroyContext(P.dpy, P.ctx);
        if (P.surf != EGL_NO_SURFACE) eglDestroySurface(P.dpy, P.surf);
        eglTerminate(P.dpy);
    }
    P.dpy = EGL_NO_DISPLAY;
    P.ctx = EGL_NO_CONTEXT;
    P.surf = EGL_NO_SURFACE;
    a->loaded_gl = 0;
}

/* ------------------------------------------------------------------ entrada */

static int32_t on_input(struct android_app *app, AInputEvent *e) {
    App *a = app->userData;
    int32_t type = AInputEvent_getType(e);
    if (type == AINPUT_EVENT_TYPE_MOTION && (AInputEvent_getSource(e) & AINPUT_SOURCE_JOYSTICK)) {
        a->lx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_X, 0);
        a->ly = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Y, 0);
        a->rx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Z, 0);
        a->ry = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RZ, 0);
        float lt = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_LTRIGGER, 0);
        float rt = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RTRIGGER, 0);
        float br = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_BRAKE, 0);
        float gs = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_GAS, 0);
        a->lt = lt > br ? lt : br;
        a->rt = rt > gs ? rt : gs;
        /* cruceta como ejes (algunos mandos) */
        float hx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_X, 0);
        float hy = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_Y, 0);
        if (fabsf(hx) > 0.5f) a->lx = hx;
        if (fabsf(hy) > 0.5f) a->ly = hy;
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int down = AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN;
        int32_t k = AKeyEvent_getKeyCode(e);
        switch (k) {
        case AKEYCODE_BUTTON_A:
        case AKEYCODE_BUTTON_B: a->fire = down; return 1;   /* misil */
        case AKEYCODE_BUTTON_L1: a->l1 = down; return 1;
        case AKEYCODE_BUTTON_R1: a->r1 = down; return 1;
        case AKEYCODE_BUTTON_L2: a->lt = down; return 1;
        case AKEYCODE_BUTTON_R2: a->rt = down; return 1;
        case AKEYCODE_DPAD_UP: a->ly = down ? -1.0f : 0; return 1;
        case AKEYCODE_DPAD_DOWN: a->ly = down ? 1.0f : 0; return 1;
        case AKEYCODE_DPAD_LEFT: a->lx = down ? -1.0f : 0; return 1;
        case AKEYCODE_DPAD_RIGHT: a->lx = down ? 1.0f : 0; return 1;
        default: break;
        }
        if (k == AKEYCODE_BUTTON_SELECT) a->sel = down;
        if (k == AKEYCODE_BUTTON_START) a->start = down;
        if (down && a->sel && a->start) { request_picker(); return 1; }
        if (!down || AKeyEvent_getRepeatCount(e)) return 1;
        /* tras un fallo de carga (pantalla roja) se puede seguir cambiando de mision o avion;
         * antes se ignoraban todos los botones y la app quedaba atascada */
        if (a->status != 1 && a->status != -2) return 1;
        switch (k) {
        case AKEYCODE_BUTTON_Y:   /* siguiente avion */
            a->plane = a->plane >= LAST_PLANE ? FIRST_PLANE : a->plane + 1;
            a->reload_mission = 1;  /* recarga todo para no mezclar texturas de pieles */
            return 1;
        case AKEYCODE_BUTTON_X:   /* avion anterior */
            a->plane = a->plane <= FIRST_PLANE ? LAST_PLANE : a->plane - 1;
            a->reload_mission = 1;
            return 1;
        case AKEYCODE_BUTTON_SELECT:  /* siguiente mision */
            a->mission = a->mission >= LAST_MISSION ? FIRST_MISSION : a->mission + 1;
            a->reload_mission = 1;
            return 1;
        case AKEYCODE_BUTTON_START:
            if (a->status == 1) reset_flight(a);
            return 1;
        default:
            return 0;
        }
    }
    return 0;
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    App *a = app->userData;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) {
            ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
            egl_init(a);
        }
        break;
    case APP_CMD_TERM_WINDOW:
        egl_term(a);
        break;
    case APP_CMD_PAUSE:
        audio_pause(1);
        break;
    case APP_CMD_RESUME:
        audio_pause(0);
        break;
    default:
        break;
    }
}

void android_main(struct android_app *app) {
    static App a;
    memset(&a, 0, sizeof a);
    P.app = app; a.platform = &P;
    P.dpy = EGL_NO_DISPLAY;
    a.mission = FIRST_MISSION;
    a.music_entry = -1;
    a.plane = 55;  /* F-15S/MT Eagle+ (acemodel.py hangar) */
    a.reload_mission = 1;
    app->userData = &a;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    a.last = a.fps_t = now();
    int first_frame = 1;
    for (;;) {
        int events;
        struct android_poll_source *src;
        while (ALooper_pollOnce(P.dpy != EGL_NO_DISPLAY ? 0 : -1, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) { egl_term(&a); return; }
        }
        if (P.dpy == EGL_NO_DISPLAY) continue;
        double t = now();
        float dt = (float)(t - a.last);
        a.last = t;
        if (dt > 0.05f) dt = 0.05f;
        if (take_disc(&a)) a.status = 0;
        if (!first_frame && a.disc.f && (a.status == 0 || a.reload_mission || a.reload_plane)) {
            load_data(&a);
            a.last = now();
        }
        if (a.status == 1) update(&a, dt);
        draw(&a);
        eglSwapBuffers(P.dpy, P.surf);
        first_frame = 0;
        a.frames++;
        a.fps_hud_frames++;
        if (t - a.fps_hud_t >= 0.5) {
            a.fps_shown = a.fps_hud_frames / (float)(t - a.fps_hud_t);
            a.fps_hud_frames = 0;
            a.fps_hud_t = t;
        }
        if (t - a.fps_t >= 2.0) {
            LOG("%.1f fps", a.frames / (t - a.fps_t));
            a.frames = 0;
            a.fps_t = t;
        }
    }
}
