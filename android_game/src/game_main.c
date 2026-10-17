/* AC3 Recomp (etapa 6, paso 4): el juego recompilado en Android.
 * COMPILACION PERSONAL: se enlaza con el codigo del juego traducido a C (data/recomp/, generado
 * desde el disco del usuario) y con ACE.BIN incrustado. No se distribuye (docs/06-legal.md).
 *
 * Hilos:
 *  - juego: ac3_main recompilado sobre la capa psx/. El reloj es el presupuesto de saltos del
 *    codigo traducido (como en psx/recomp/native_main.c); cada VBlank espera al tiempo real
 *    (50 Hz, PAL) y deja la imagen visible en un bufer compartido;
 *  - principal: EGL, presentacion a 4:3 (psx/gl/psx_present.c), entrada y ciclo de vida;
 *  - imagenes intermedias (psx/src/interp.c, pasos 5 y 6): en la mision el juego dibuja 25
 *    imagenes por segundo (una por paso de logica); la capa entrega hasta 3 intermedias + la real
 *    por paso, cada una con su hora de salida repartida en la duracion del paso (con un paso de
 *    retraso), y la pantalla muestra en cada refresco la ultima cuya hora ya paso. Las intermedias
 *    se dibujan en 3 hilos aparte (paso 6: en la 1.1 iban dentro del hilo del juego y en la
 *    Retroid lo frenaban). L3+R3 lo activa o desactiva;
 *  - el juego y los hilos de dibujo van fijados a los nucleos rapidos (los de mayor frecuencia);
 *  - contador (arriba a la izquierda): imagenes distintas mostradas por segundo, velocidad del
 *    juego frente a la consola (100% = 50 VBlank por segundo), intermedias por paso y uso del
 *    hilo del juego. Las mismas cifras van a logcat (etiqueta AC3Game) cada segundo;
 *  - AAudio: lee un anillo de muestras. La SPU de la capa no tiene cerrojos, asi que las
 *    muestras se generan en el hilo del juego (882 por fotograma: 44100 Hz / 50). */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "psx_display.h"
#include "psx_interp.h"
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
    /* cola de imagenes con hora de salida (interpolacion) */
    struct { uint32_t px[640 * 512]; int w, h; double due; int full; } q[8];
    double interp_last;              /* ultima imagen entregada por la interpolacion */
    volatile int interp_enabled;
    uint32_t show[640 * 512];        /* copia de lo que se presenta (fuera del cerrojo) */
    /* cifras del contador, publicadas por el hilo del juego cada segundo */
    volatile int st_cpu, st_nfrac, st_core, st_skip, st_steps;
    volatile float st_worker_ms, st_real_ms;
    int16_t ring[16384 * 2];       /* anillo de audio estereo (potencia de dos) */
    volatile uint32_t ring_w, ring_r;
} G = {.lock = PTHREAD_MUTEX_INITIALIZER, .disc_fd = -1, .interp_enabled = 1};

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
static void interp_sink(const uint32_t *rgba, int w, int h, int num, int den, int vb, double t_step);

static int g_nfrac = 3;   /* intermedias por paso (adaptativo) */

static void frame_wait(void) {
    static double next;
    double t = now();
    while (G.paused && G.running) { usleep(20000); next = 0; }
    if (next == 0 || t - next > 0.2) next = t;   /* tras una pausa o un retraso grande, sin recuperar */
    next += 1.0 / 50;
    double slack = next - t;
    if (slack > 0) usleep((useconds_t)(slack * 1e6));
    /* cada 50 VBlank: uso del hilo del juego y numero de intermedias adaptativo. Se baja si los
     * hilos de dibujo no terminan a tiempo (pasos saltados) o si el hilo del juego va justo; se
     * sube si sobra (de 1 a 3) */
    static double sleep_sum, win_t0;
    static int sleep_n;
    sleep_sum += slack > 0 ? slack : 0;
    if (win_t0 == 0) win_t0 = t;
    if (++sleep_n == 50) {
        double tn = now(), span = tn - win_t0;
        double avg = sleep_sum / sleep_n * 1000;   /* ms dormidos por VBlank de 20 ms */
        PsxInterpPerf p;
        psx_interp_perf(&p, 1);
        int want = g_nfrac;
        if (p.steps >= 5 && p.skipped * 10 > p.steps) want = g_nfrac - 1;
        else if (avg < 3) want = g_nfrac - 1;
        else if (p.steps >= 5 && p.skipped == 0 && p.worker_max_ms < 25 && avg > 8) want = g_nfrac + 1;
        want = want < 1 ? 1 : want > 3 ? 3 : want;
        if (want != g_nfrac && G.interp_enabled) {
            g_nfrac = want;
            psx_interp_config(g_nfrac, 2, interp_sink);
            LOG("imagenes intermedias por paso: %d (dormido %.1f ms por VBlank, %d de %d pasos saltados)", g_nfrac, avg,
                p.skipped, p.steps);
        }
        G.st_cpu = span > 0 ? (int)lrint(100.0 * (1.0 - sleep_sum / span)) : 0;
        G.st_nfrac = G.interp_enabled ? g_nfrac : 0;
        G.st_core = sched_getcpu();
        G.st_skip = p.skipped;
        G.st_steps = p.steps;
        G.st_worker_ms = (float)p.worker_ms;
        G.st_real_ms = (float)(p.pair_ms + p.snap_ms + p.real_ms);
        sleep_sum = 0;
        sleep_n = 0;
        win_t0 = tn;
    }
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
    /* L3+R3 cambia G.interp_enabled en el hilo principal; aqui se aplica (hilo del juego) */
    static int interp_applied = 1;
    if (interp_applied != G.interp_enabled) {
        interp_applied = G.interp_enabled;
        psx_interp_config(interp_applied ? g_nfrac : 0, 2, interp_sink);
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

/* sumidero de la interpolacion (hilo del juego para la real; hilos de dibujo para las
 * intermedias): hora de salida = hora del paso + fase */
static void interp_sink(const uint32_t *rgba, int w, int h, int num, int den, int vb, double t_step) {
    if (w * h > 640 * 512) return;
    double due = t_step + (double)num / den * vb / 50.0;
    pthread_mutex_lock(&G.lock);
    int k = 0;   /* la ranura libre o la mas antigua */
    for (int i = 0; i < 8; i++) {
        if (!G.q[i].full) { k = i; break; }
        if (G.q[i].due < G.q[k].due) k = i;
    }
    memcpy(G.q[k].px, rgba, (size_t)w * h * 4);
    G.q[k].w = w; G.q[k].h = h; G.q[k].due = due; G.q[k].full = 1;
    G.interp_last = now();
    pthread_mutex_unlock(&G.lock);
}

/* nucleos rapidos: los de frecuencia maxima >= 75% de la mayor (en el Snapdragon 865 de la
 * Retroid, los 4 Cortex-A77: cpu4-7). Se aplica al hilo que llama */
static void pin_fast_cores(void) {
    long f[32] = {0}, top = 0;
    for (int i = 0; i < 32; i++) {
        char path[96];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;
        if (fscanf(fp, "%ld", &f[i]) != 1) f[i] = 0;
        fclose(fp);
        if (f[i] > top) top = f[i];
    }
    if (!top) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    int n = 0;
    for (int i = 0; i < 32; i++)
        if (f[i] * 4 >= top * 3) { CPU_SET(i, &set); n++; }
    int r = sched_setaffinity(0, sizeof set, &set);
    LOG("hilo fijado a %d nucleos rapidos (%ld MHz): %s", n, top / 1000, r == 0 ? "si" : "no");
}

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
    pin_fast_cores();
    psx_interp_config(g_nfrac, 2, interp_sink);   /* luego se ajusta solo (frame_wait) */
    psx_interp_threads(3, pin_fast_cores);
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
        if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN) {
            G.keys |= m;
            /* L3+R3: imagenes intermedias si/no (para comparar con el original) */
            if ((G.keys & (PSX_PAD_L3 | PSX_PAD_R3)) == (PSX_PAD_L3 | PSX_PAD_R3) && AKeyEvent_getRepeatCount(e) == 0) {
                G.interp_enabled = !G.interp_enabled;
                LOG("imagenes intermedias: %s", G.interp_enabled ? "si" : "no");
            }
        }
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

/* ------------------------------------------------------------------ contador */

static const struct { char c; uint8_t row[7]; } FONT[] = {
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
    {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
    {'3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
    {'5', {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}},
    {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
    {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
    {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'C', {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'%', {0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13}},
    {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
};

/* texto de 5x7 en la imagen (pixeles de la PS1), sobre una franja oscura */
static void draw_text(uint32_t *img, int w, int h, int x0, int y0, const char *txt) {
    int len = (int)strlen(txt);
    for (int y = y0 - 1; y < y0 + 8 && y < h; y++)
        for (int x = x0 - 1; x < x0 + len * 6 && x < w; x++)
            if (y >= 0 && x >= 0) img[y * w + x] = 0xFF000000u;
    for (int i = 0; i < len; i++) {
        const uint8_t *g = NULL;
        for (unsigned k = 0; k < sizeof FONT / sizeof FONT[0]; k++)
            if (FONT[k].c == txt[i]) g = FONT[k].row;
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (g[r] >> (4 - c) & 1) {
                    int x = x0 + i * 6 + c, y = y0 + r;
                    if (x < w && y < h) img[y * w + x] = 0xFF40FF40u;
                }
    }
}

/* resumen de la imagen (sin las filas del contador) para contar imagenes distintas */
static uint32_t frame_hash(const uint32_t *img, int w, int h) {
    uint32_t x = 2166136261u;
    for (int y = 20; y < h; y += 3)
        for (int i = (y * 7) % 5; i < w; i += 5) x = (x ^ img[y * w + i]) * 16777619u;
    return x ^ (uint32_t)(w * 4099 + h);
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
        /* la imagen que toca, copiada bajo el cerrojo y presentada fuera de el */
        pthread_mutex_lock(&G.lock);
        double t = now();
        int best = -1, sw = 0, sh = 0;   /* interpolacion activa: la ultima imagen cuya hora ya llego */
        if (G.interp_enabled && t - G.interp_last < 0.15) {
            for (int i = 0; i < 8; i++)
                if (G.q[i].full && G.q[i].due <= t && (best < 0 || G.q[i].due > G.q[best].due)) best = i;
            if (best >= 0)
                for (int i = 0; i < 8; i++)
                    if (G.q[i].full && G.q[i].due < G.q[best].due) G.q[i].full = 0;
        }
        if (best >= 0) { sw = G.q[best].w; sh = G.q[best].h; memcpy(G.show, G.q[best].px, (size_t)sw * sh * 4); }
        else if (G.fw && G.fh) { sw = G.fw; sh = G.fh; memcpy(G.show, G.frame, (size_t)sw * sh * 4); }
        pthread_mutex_unlock(&G.lock);
        /* contador: imagenes distintas por segundo y velocidad del juego (VBlank por segundo) */
        static double c_t0;
        static uint32_t c_last, c_vb0;
        static int c_new, c_fps, c_vel;
        static char line1[48], line2[48];
        if (sw && sh) {
            uint32_t hsh = frame_hash(G.show, sw, sh);
            if (hsh != c_last) { c_new++; c_last = hsh; }
        }
        if (c_t0 == 0) { c_t0 = t; c_vb0 = (uint32_t)VSync(-1); }
        if (t - c_t0 >= 1.0) {
            uint32_t vb = (uint32_t)VSync(-1);
            c_fps = (int)lrint(c_new / (t - c_t0));
            c_vel = (int)lrint((vb - c_vb0) / (t - c_t0) * 2.0);   /* % de 50 VBlank/s */
            snprintf(line1, sizeof line1, "FPS %d  VEL %d%%", c_fps, c_vel);
            if (G.st_nfrac) snprintf(line2, sizeof line2, "INT %d  CPU %d%%", G.st_nfrac, G.st_cpu);
            else snprintf(line2, sizeof line2, "INT -  CPU %d%%", G.st_cpu);
            if (G.running)
                LOG("fps %d, velocidad %d%%, intermedias %d, hilo del juego %d%% (cpu%d), pasos %d (saltados %d), "
                    "ms por paso: juego %.1f, cada hilo de dibujo %.1f", c_fps, c_vel, G.st_nfrac, G.st_cpu, G.st_core,
                    G.st_steps, G.st_skip, G.st_real_ms, G.st_worker_ms);
            c_t0 = t; c_vb0 = vb; c_new = 0;
        }
        if (sw && sh) {
            if (line1[0]) { draw_text(G.show, sw, sh, 4, 4, line1); draw_text(G.show, sw, sh, 4, 13, line2); }
            psx_present_frame(G.show, sw, sh, G.w, G.h, PSX_FIT_43, 0);
        } else { glViewport(0, 0, G.w, G.h); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); }
        eglSwapBuffers(G.dpy, G.surf);
    }
}
