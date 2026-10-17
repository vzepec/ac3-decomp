/* AC3 Recomp (etapa 6, paso 4): el juego recompilado en Android.
 * COMPILACION PERSONAL: se enlaza con el codigo del juego traducido a C (data/recomp/, generado
 * desde el disco del usuario) y con ACE.BIN incrustado. No se distribuye (docs/06-legal.md).
 *
 * Hilos:
 *  - juego: ac3_main recompilado sobre la capa psx/. El reloj es el presupuesto de saltos del
 *    codigo traducido (como en psx/recomp/native_main.c); cada VBlank espera al tiempo real
 *    (50 Hz, PAL) y deja la imagen visible en un bufer compartido;
 *  - principal: EGL, presentacion a 4:3 (psx/gl/psx_present.c), entrada y ciclo de vida;
 *  - AAudio: lee un anillo de muestras. La SPU de la capa no tiene cerrojos, asi que las
 *    muestras se generan en el hilo del juego (882 por fotograma: 44100 Hz / 50). */
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "psx_display.h"
#include "psx_libcard.h"
#include "psx_libcd.h"
#include "psx_libetc.h"
#include "psx_libgte.h"
#include "psx_libpad.h"
#include "psx_libspu.h"
#include "psx_mem.h"
#include "psx_spu.h"
#include "psx_xa.h"
#include "recomp_rt.h"
#include "../../psx/gl/psx_present.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "AC3Game", __VA_ARGS__)

extern const RcFunc rc_funcs_ace_eu[], rc_funcs_overlay1[], rc_funcs_overlay2[], rc_funcs_overlay3[],
    rc_funcs_overlay4[], rc_funcs_overlay5[];
extern const unsigned rc_nfuncs_ace_eu, rc_nfuncs_overlay1, rc_nfuncs_overlay2, rc_nfuncs_overlay3,
    rc_nfuncs_overlay4, rc_nfuncs_overlay5;
extern const RcFunc hle_table[];
extern const unsigned hle_count;
extern const uint8_t ovl_sig[5][64];
extern const uint8_t ace_image[];
extern const unsigned ace_image_len;
void rc_register(const RcFunc *funcs, unsigned n);
void rc_register_overlay(int n, const RcFunc *funcs, unsigned count);

#define AC3_MAIN 0x80025D08u
#define BUDGET_PER_FRAME 25000   /* como psx/recomp/native_main.c (aproximado) */

static struct {
    struct android_app *app;
    EGLDisplay dpy;
    EGLSurface surf;
    EGLContext ctx;
    int w, h;
    pthread_mutex_t lock;          /* imagen compartida juego -> pantalla */
    uint32_t frame[640 * 512];
    int fw, fh;
    pthread_t game;
    volatile int running, paused;
    volatile int disc_fd;
    char card_path[512];
    volatile uint16_t buttons, keys;
    volatile float lx, ly, rx, ry;
    AAudioStream *audio;
    int16_t ring[16384 * 2];       /* anillo de audio estereo (potencia de dos) */
    volatile uint32_t ring_w, ring_r;
} G = {.lock = PTHREAD_MUTEX_INITIALIZER, .disc_fd = -1};

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ disco (selector) */

JNIEXPORT void JNICALL Java_com_vzepec_ac3game_MainActivity_nativeSetDisc(JNIEnv *env, jclass cls, jint fd) {
    (void)env; (void)cls;
    int old = __atomic_exchange_n(&G.disc_fd, fd, __ATOMIC_SEQ_CST);
    if (old >= 0) close(old);
}

/* ------------------------------------------------------------------ hilo del juego */

static uint8_t to_axis(float v) {
    int x = (int)lrintf(128.0f + v * 127.0f);
    return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
}

/* antes de cada VBlank: ritmo real (50 Hz), mando e imagen para la pantalla */
static void frame_wait(void) {
    static double next;
    double t = now();
    while (G.paused && G.running) { usleep(20000); next = 0; }
    if (next == 0 || t - next > 0.2) next = t;   /* tras una pausa o un retraso grande, sin recuperar */
    next += 1.0 / 50;
    if (next > t) usleep((useconds_t)((next - t) * 1e6));
    /* audio de este fotograma; si AAudio va atrasado (anillo casi lleno) no se acumula */
    static int16_t pcm[882 * 2];
    psx_spu_render(psx_spu(), pcm, 882);
    uint32_t wpos = G.ring_w;
    if (wpos - G.ring_r <= 16384 - 882) {
        for (int i = 0; i < 882; i++) {
            G.ring[((wpos + i) & 16383) * 2] = pcm[i * 2];
            G.ring[((wpos + i) & 16383) * 2 + 1] = pcm[i * 2 + 1];
        }
        __atomic_store_n(&G.ring_w, wpos + 882, __ATOMIC_RELEASE);
    }
    psx_pad_set(0, (uint16_t)(G.buttons | G.keys), to_axis(G.lx), to_axis(G.ly), to_axis(G.rx), to_axis(G.ry));
    static uint32_t img[640 * 512];
    int w, h;
    psx_display_frame(img, 640 * 512, &w, &h);
    pthread_mutex_lock(&G.lock);
    if (w && h) { memcpy(G.frame, img, (size_t)w * h * 4); G.fw = w; G.fh = h; }
    pthread_mutex_unlock(&G.lock);
}

static void tick(void) { psx_vblank(); }

static void *game_thread(void *arg) {
    (void)arg;
    memcpy(psx_ram + 0x10000, ace_image, ace_image_len);
    psx_gte_tables_init(ace_image, ace_image_len);
    psx_pad_connect(0, 0x73);   /* analogico: los sticks de la Retroid */
    psx_pad_connect(1, 0);
    psx_card_attach(0, G.card_path);
    psx_card_detach(1);
    psx_set_refresh(50);
    psx_audio_connect();        /* audio XA del CD -> entrada de CD de la SPU */
    rc_register(rc_funcs_ace_eu, rc_nfuncs_ace_eu);
    rc_register_overlay(1, rc_funcs_overlay1, rc_nfuncs_overlay1);
    rc_register_overlay(2, rc_funcs_overlay2, rc_nfuncs_overlay2);
    rc_register_overlay(3, rc_funcs_overlay3, rc_nfuncs_overlay3);
    rc_register_overlay(4, rc_funcs_overlay4, rc_nfuncs_overlay4);
    rc_register_overlay(5, rc_funcs_overlay5, rc_nfuncs_overlay5);
    rc_set_overlay_sigs(ovl_sig, 5);
    rc_set_hle(hle_table, hle_count);
    rc_gte = (PsxGte *)psx_gte_get_context();
    psx_set_call(rc_host_callback);
    psx_set_frame_wait(frame_wait);
    rc_set_tick(tick, BUDGET_PER_FRAME);
    memset(psx_ram + 0xBACB0, 0, 0xC9DC0 - 0xBACB0);   /* BSS, como ac3_start */
    RCpu c;
    memset(&c, 0, sizeof c);
    c.r[28] = 0x800BA9F4u;
    c.r[29] = 0x801FFF00u;
    c.r[31] = 0x8000FFF0u;
    rc_ret = c.r[31];
    LOG("arranca ac3_main");
    rc_call(&c, AC3_MAIN);   /* no vuelve */
    LOG("ac3_main ha vuelto");
    return NULL;
}

/* ------------------------------------------------------------------ audio */

static aaudio_data_callback_result_t audio_cb(AAudioStream *s, void *user, void *data, int32_t frames) {
    (void)s; (void)user;
    int16_t *out = data;
    uint32_t r = G.ring_r, w = __atomic_load_n(&G.ring_w, __ATOMIC_ACQUIRE);
    for (int32_t i = 0; i < frames; i++) {
        if (r != w && !G.paused) {
            out[i * 2] = G.ring[(r & 16383) * 2];
            out[i * 2 + 1] = G.ring[(r & 16383) * 2 + 1];
            r++;
        } else {
            out[i * 2] = out[i * 2 + 1] = 0;
        }
    }
    __atomic_store_n(&G.ring_r, r, __ATOMIC_RELEASE);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void audio_start(void) {
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, 44100);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, audio_cb, NULL);
    if (AAudioStreamBuilder_openStream(b, &G.audio) == AAUDIO_OK) AAudioStream_requestStart(G.audio);
    AAudioStreamBuilder_delete(b);
}

/* ------------------------------------------------------------------ EGL */

static void egl_init(void) {
    const EGLint attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    const EGLint cattr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig cfg;
    EGLint n;
    G.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(G.dpy, NULL, NULL);
    eglChooseConfig(G.dpy, attr, &cfg, 1, &n);
    G.surf = eglCreateWindowSurface(G.dpy, cfg, G.app->window, NULL);
    G.ctx = eglCreateContext(G.dpy, cfg, EGL_NO_CONTEXT, cattr);
    eglMakeCurrent(G.dpy, G.surf, G.surf, G.ctx);
    eglSwapInterval(G.dpy, 1);
    eglQuerySurface(G.dpy, G.surf, EGL_WIDTH, &G.w);
    eglQuerySurface(G.dpy, G.surf, EGL_HEIGHT, &G.h);
    psx_present_init();
    LOG("EGL %dx%d", G.w, G.h);
}

static void egl_term(void) {
    if (G.dpy == EGL_NO_DISPLAY) return;
    psx_present_shutdown();
    eglMakeCurrent(G.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(G.dpy, G.ctx);
    eglDestroySurface(G.dpy, G.surf);
    eglTerminate(G.dpy);
    G.dpy = EGL_NO_DISPLAY;
}

/* ------------------------------------------------------------------ entrada */

static uint16_t key_button(int32_t k) {
    switch (k) {
    case AKEYCODE_BUTTON_A: return PSX_PAD_CROSS;
    case AKEYCODE_BUTTON_B: return PSX_PAD_CIRCLE;
    case AKEYCODE_BUTTON_X: return PSX_PAD_SQUARE;
    case AKEYCODE_BUTTON_Y: return PSX_PAD_TRIANGLE;
    case AKEYCODE_BUTTON_L1: return PSX_PAD_L1;
    case AKEYCODE_BUTTON_R1: return PSX_PAD_R1;
    case AKEYCODE_BUTTON_L2: return PSX_PAD_L2;
    case AKEYCODE_BUTTON_R2: return PSX_PAD_R2;
    case AKEYCODE_BUTTON_THUMBL: return PSX_PAD_L3;
    case AKEYCODE_BUTTON_THUMBR: return PSX_PAD_R3;
    case AKEYCODE_BUTTON_START: return PSX_PAD_START;
    case AKEYCODE_BUTTON_SELECT: return PSX_PAD_SELECT;
    case AKEYCODE_DPAD_UP: return PSX_PAD_UP;
    case AKEYCODE_DPAD_DOWN: return PSX_PAD_DOWN;
    case AKEYCODE_DPAD_LEFT: return PSX_PAD_LEFT;
    case AKEYCODE_DPAD_RIGHT: return PSX_PAD_RIGHT;
    default: return 0;
    }
}

static int32_t on_input(struct android_app *app, AInputEvent *e) {
    (void)app;
    int32_t type = AInputEvent_getType(e);
    if (type == AINPUT_EVENT_TYPE_MOTION && (AInputEvent_getSource(e) & AINPUT_SOURCE_JOYSTICK)) {
        G.lx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_X, 0);
        G.ly = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Y, 0);
        G.rx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_Z, 0);
        G.ry = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RZ, 0);
        /* gatillos analogicos y cruceta como ejes (segun el mando) */
        float lt = fmaxf(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_LTRIGGER, 0), AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_BRAKE, 0));
        float rt = fmaxf(AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_RTRIGGER, 0), AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_GAS, 0));
        float hx = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_X, 0), hy = AMotionEvent_getAxisValue(e, AMOTION_EVENT_AXIS_HAT_Y, 0);
        uint16_t b = 0;
        if (lt > 0.5f) b |= PSX_PAD_L2;
        if (rt > 0.5f) b |= PSX_PAD_R2;
        if (hx < -0.5f) b |= PSX_PAD_LEFT;
        if (hx > 0.5f) b |= PSX_PAD_RIGHT;
        if (hy < -0.5f) b |= PSX_PAD_UP;
        if (hy > 0.5f) b |= PSX_PAD_DOWN;
        G.buttons = b;   /* lo que llega como eje; lo que llega como tecla va en G.keys */
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY) {
        uint16_t m = key_button(AKeyEvent_getKeyCode(e));
        if (!m) return 0;
        if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN) G.keys |= m;
        else if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_UP) G.keys &= (uint16_t)~m;
        return 1;
    }
    return 0;
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) {
            ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
            egl_init();
        }
        break;
    case APP_CMD_TERM_WINDOW: egl_term(); break;
    case APP_CMD_PAUSE: G.paused = 1; break;
    case APP_CMD_RESUME: G.paused = 0; break;
    default: break;
    }
}

void android_main(struct android_app *app) {
    G.app = app;
    G.dpy = EGL_NO_DISPLAY;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    snprintf(G.card_path, sizeof G.card_path, "%s/tarjeta1.mcd", app->activity->internalDataPath);
    for (;;) {
        int events;
        struct android_poll_source *src;
        while (ALooper_pollOnce(G.dpy != EGL_NO_DISPLAY ? 0 : -1, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) { G.running = 0; egl_term(); _exit(0); }
        }
        if (G.dpy == EGL_NO_DISPLAY) continue;
        /* el disco elegido arranca el juego (una vez) */
        int fd = __atomic_exchange_n(&G.disc_fd, -1, __ATOMIC_SEQ_CST);
        if (fd >= 0 && !G.running) {
            if (psx_cd_open_fd(fd) == 0) {
                G.running = 1;
                audio_start();
                pthread_attr_t at;   /* pila grande: el codigo traducido anida mucho */
                pthread_attr_init(&at);
                pthread_attr_setstacksize(&at, 64u << 20);
                pthread_create(&G.game, &at, game_thread, NULL);
                pthread_attr_destroy(&at);
                LOG("juego en marcha");
            } else {
                close(fd);
                LOG("no se puede leer el disco");
            }
        } else if (fd >= 0) {
            close(fd);
        }
        pthread_mutex_lock(&G.lock);
        if (G.fw && G.fh) psx_present_frame(G.frame, G.fw, G.fh, G.w, G.h, PSX_FIT_43, 0);
        else { glViewport(0, 0, G.w, G.h); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); }
        pthread_mutex_unlock(&G.lock);
        eglSwapBuffers(G.dpy, G.surf);
    }
}
