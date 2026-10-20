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
 *    Retroid lo frenaban);
 *  - pantalla ancha (paso 16): todo se presenta a 16:9 salvo los videos (color de 24 bits), que
 *    siguen a 4:3. En la mision, ademas, la GTE comprime la x a 3/4 (psx_gte_wide) para que la
 *    escena 3D no salga deformada y el cono de vision del terreno se ensancha
 *    (ac3_wide_install); los menus solo se estiran;
 *  - interpolacion nueva (etapa 8, psx/recomp/ac3_rerender.c): en cada imagen intermedia el mundo
 *    estatico (terreno, cielo, ciudades) se vuelve a dibujar con el codigo del juego y la camara a
 *    medio camino; solo lo movil se interpola en pantalla. Una intermedia por paso (30 -> 60 fps).
 *    la interpolacion anterior, la de la etapa 6, se quito de la app en la 1.9-alfa (sin la nueva,
 *    en los APK personales, es la que se usa);
 *  - dibujo por la GPU (etapa 8, paso 1; psx/gl/psx_hw.c): las imagenes de la mision (real e
 *    intermedias) llegan como la lista de primitivas anotada (psx_interp_hw) y se dibujan con GLES,
 *    por ahora a escala 1 y con el tramado de la PS1 (la misma imagen que por software). Las
 *    intermedias ya no se rasterizan en la CPU; en la mision, la real tampoco (paso 2: la GPU
 *    conserva la imagen). L3+R3 recorre: 16:9 -> 4:3 -> el original (software, 4:3, sin intermedias);
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
#include <sys/stat.h>
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
#include "../../psx/gl/psx_hw.h"
#include "touchpad.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "AC3Game", __VA_ARGS__)

/* la version del juego (europea o americana): la elige el disco (disc_region); el APK
 * universal lleva las dos (ac3_regions.h, regions.c) */
#ifdef AC3_LIBRE
/* etapa 7, APK libre (sin nada de Namco): la version se detecta en el disco, ACE.BIN se lee y
 * descomprime de el y todo el codigo del juego lo ejecuta el interprete (psx/recomp/mipsi.c) */
#include "ac3_versions.h"
#include "ac3_rerender.h"
#include "ac3_rr_async.h"
#include "psx_acebin.h"
static const Ac3Version *R;
static uint8_t g_ace[0x200000];
static PsxAceBin g_ab;
#else
#include "ac3_regions.h"
static const Ac3Region *R;
#endif
static int g_refresh = 50, g_spf = 882;   /* VBlank por segundo; muestras de audio por VBlank */
void rc_register(const RcFunc *funcs, unsigned n);
void rc_register_overlay(int n, const RcFunc *funcs, unsigned count);

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
    struct {
        uint32_t px[640 * 512]; int w, h; double due; int full, wide;
        /* GPU (hw = 1): lista anotada, sus subidas y la VRAM de partida; seq numera las imagenes */
        int hw, x0, y0, np, nd, real, stale;
        uint32_t seq;
        PsxHwPrim *prims; int capp;
        uint16_t *data; int capd;
        uint16_t *bg;
    } q[8];
    uint32_t seq;
    double interp_last;              /* ultima imagen entregada por la interpolacion */
    volatile int interp_enabled;
    /* L3+R3 (v1.10-alfa): 0 -> 2 -> 3. 0 = GPU + 16:9, 1 = software + 16:9 (fuera del ciclo), 2 = GPU + 4:3,
     * 3 = original (software, 4:3, sin intermedias) */
    volatile int gfx_mode;
    volatile int show_stats;       /* contador y estadisticas en pantalla (opcion del menu de inicio) */
    volatile int scale;            /* resolucion interna de la GPU (etapa 8, paso 4; opcion del menu): 1-4 */
    volatile int filter;           /* filtro de texturas: 0 el de la PS1, 1 bilineal (opcion del menu) */
    /* depuracion por adb (etapa 8, paso 4): <externalDataPath>/depurar.txt, releido cada segundo:
     * "escala=N color24=0|1 solo=0|1 filtro=0|1 modo=0-3" (-1: lo normal; modo: el de L3+R3, 1 = software
     * a 16:9 con intermedias, fuera del ciclo) */
    char dbg_path[512];
    volatile int dbg_scale, dbg_true, dbg_only, dbg_filter, dbg_mode;
    volatile int rr_on;              /* la interpolacion nueva esta activa (una intermedia por paso) */
    volatile int wide_now;           /* la GTE proyecta en pantalla ancha (mision con modo 0 o 1) */
    int frame_wide;                  /* G.frame se presenta a 16:9 (modo 0 o 1 y no es un video) */
    uint32_t show[640 * 512];        /* copia de lo que se presenta (fuera del cerrojo) */
    /* cifras del contador, publicadas por el hilo del juego cada segundo */
    volatile int st_cpu, st_nfrac, st_core, st_skip, st_steps, st_unpaired;
    volatile float st_worker_ms, st_real_ms, st_rr_ms;
    int16_t ring[16384 * 2];       /* anillo de audio estereo (potencia de dos) */
    volatile uint32_t ring_w, ring_r;
} G = {.lock = PTHREAD_MUTEX_INITIALIZER, .disc_fd = -1, .interp_enabled = 1, .show_stats = 1, .scale = 1,
       .dbg_scale = -1, .dbg_true = -1, .dbg_only = -1, .dbg_filter = -1, .dbg_mode = -1};

static void dbg_read(void) {
    FILE *f = G.dbg_path[0] ? fopen(G.dbg_path, "r") : NULL;
    int s = -1, t = -1, o = -1, fl = -1, md = -1;
    if (f) {
        char k[32];
        int v;
        while (fscanf(f, " %31[^=]=%d", k, &v) == 2) {
            if (!strcmp(k, "escala")) s = v;
            else if (!strcmp(k, "color24")) t = v;
            else if (!strcmp(k, "solo")) o = v;
            else if (!strcmp(k, "filtro")) fl = v;
            else if (!strcmp(k, "modo")) md = v;
        }
        fclose(f);
    }
    if (s != G.dbg_scale || t != G.dbg_true || o != G.dbg_only || fl != G.dbg_filter || md != G.dbg_mode)
        LOG("depurar: escala %d, color24 %d, solo %d, filtro %d, modo %d", s, t, o, fl, md);
    G.dbg_scale = s; G.dbg_true = t; G.dbg_only = o; G.dbg_filter = fl;
    if (md >= 0 && md <= 3 && md != G.dbg_mode) G.gfx_mode = md;
    G.dbg_mode = md;
}

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

/* opciones del menu de inicio (LauncherActivity), antes de arrancar el juego */
/* graficos del mando virtual (los dibuja MainActivity) */
JNIEXPORT void JNICALL Java_com_vzepec_ac3game_MainActivity_nativeSetTouchAtlas(JNIEnv *env, jclass cls, jintArray px, jint w, jint h) {
    (void)cls;
    jint *p = (*env)->GetIntArrayElements(env, px, NULL);
    if (p) { touchpad_set_atlas((const uint32_t *)p, w, h); (*env)->ReleaseIntArrayElements(env, px, p, JNI_ABORT); }
}

JNIEXPORT void JNICALL Java_com_vzepec_ac3game_MainActivity_nativeSetOptions(JNIEnv *env, jclass cls, jboolean stats, jint scale,
                                                                             jint filter, jint touch) {
    (void)env; (void)cls;
    G.show_stats = stats;   /* (el registro de adb logcat sigue igual) */
    G.scale = scale < 1 ? 1 : scale > 4 ? 4 : scale;
    G.filter = filter == 1;
    touchpad_set_mode(touch >= 0 && touch <= 2 ? touch : 0);
    LOG("opciones: contadores %d, resolucion interna x%d, filtro %d, mando tactil %d", stats, G.scale, G.filter, touch);
}

/* ------------------------------------------------------------------ hilo del juego */

static uint8_t to_axis(float v) {
    int x = (int)lrintf(128.0f + v * 127.0f);
    return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
}

/* antes de cada VBlank: ritmo real (50 Hz PAL, 60 Hz NTSC), mando e imagen para la pantalla */
static void interp_sink(const uint32_t *rgba, int w, int h, int num, int den, int vb, double t_step);
static void hw_sink(const PsxHwList *l, const uint16_t *bg, const uint16_t *sw, int x0, int y0, int w, int h, int num, int den,
                    int vb, double t_step);
/* medida de la GPU (temporal): 4 = GPU sin el paso 2, 5 = todas las subidas adelantadas, 6 = sin la
 * pasada de mascara; las tres a 16:9. L3+R3: 0 -> 4 -> 5 -> 6 -> 1 -> 2 -> 3 */
#define MODE_WIDE(m) ((m) <= 1 || (m) >= 4)
#define MODE_HW(m) ((m) == 0 || (m) == 2 || (m) >= 4)
extern int psx_hw_hoist_all, psx_hw_no_maskpass, psx_hw_onepass, psx_hw_stat_cut[4];

static int g_nfrac = 3;   /* intermedias por paso (adaptativo) */
static int g_late_real, g_late_inter, g_drop_inter;   /* medicion: llegan tarde a su hora; intermedias tiradas sin dibujar */
static float g_worker_max;

static void frame_wait(void) {
    static double next;
    double t = now();
    while (G.paused && G.running) { usleep(20000); next = 0; }
    if (next == 0 || t - next > 0.2) next = t;   /* tras una pausa o un retraso grande, sin recuperar */
    next += 1.0 / g_refresh;
    double slack = next - t;
    if (slack > 0) usleep((useconds_t)(slack * 1e6));
    /* cada 50 VBlank: uso del hilo del juego y numero de intermedias adaptativo. Se baja si los
     * hilos de dibujo no terminan a tiempo (pasos saltados) o si el hilo del juego va justo; se
     * sube si sobra (de 1 a 3) */
    static double sleep_sum, win_t0;
    static int sleep_n;
    sleep_sum += slack > 0 ? slack : 0;
    if (win_t0 == 0) win_t0 = t;
    if (++sleep_n == g_refresh) {
        double tn = now(), span = tn - win_t0;
        double avg = sleep_sum / sleep_n * 1000;   /* ms dormidos por VBlank de 20 ms */
        PsxInterpPerf p;
        psx_interp_perf(&p, 1);
        /* subir solo tras 5 ventanas seguidas holgadas: cada cambio altera la cadencia */
        static int good;
        int want = g_nfrac;
        if (p.steps >= 5 && p.skipped * 10 > p.steps) want = g_nfrac - 1, good = 0;
        else if (avg < 3) want = g_nfrac - 1, good = 0;
        else if (p.steps >= 5 && p.skipped == 0 && p.worker_max_ms < 25 && avg > 8) { if (++good >= 5) want = g_nfrac + 1, good = 0; }
        else good = 0;
        want = want < 1 ? 1 : want > (G.rr_on ? 1 : 3) ? (G.rr_on ? 1 : 3) : want;   /* la nueva: 30 -> 60 fps con una */
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
        G.st_unpaired = p.unpaired;
        G.st_worker_ms = (float)p.worker0_ms;
        G.st_rr_ms = (float)p.rebuild_ms;
        g_worker_max = (float)p.worker_max_ms;
        G.st_real_ms = (float)(p.pair_ms + p.snap_ms + p.real_ms);
        sleep_sum = 0;
        sleep_n = 0;
        win_t0 = tn;
    }
    /* audio de este fotograma; si AAudio va atrasado (anillo casi lleno) no se acumula */
    static int16_t pcm[882 * 2];   /* el mayor: 882 (PAL); 735 en NTSC */
    psx_spu_render(psx_spu(), pcm, g_spf);
    uint32_t wpos = G.ring_w;
    if (wpos - G.ring_r <= 16384 - (uint32_t)g_spf) {
        for (int i = 0; i < g_spf; i++) {
            G.ring[((wpos + i) & 16383) * 2] = pcm[i * 2];
            G.ring[((wpos + i) & 16383) * 2 + 1] = pcm[i * 2 + 1];
        }
        __atomic_store_n(&G.ring_w, wpos + (uint32_t)g_spf, __ATOMIC_RELEASE);
    }
    /* L3+R3 cambia G.gfx_mode en el hilo principal; aqui se aplica (hilo del juego) */
    static int interp_applied = 1, hw_applied = -1;
    G.interp_enabled = G.gfx_mode != 3;
#ifdef AC3_LIBRE
    static int rr_applied = -1;
    int rr = G.gfx_mode != 3;
    if (rr != rr_applied) {
        rr_applied = rr;
        G.rr_on = rr;
        /* el redibujo, en un hilo de dibujo (segunda copia del interprete): el del juego solo deja
         * una instantanea por paso */
        psx_interp_rebuild_async(rr ? ac3_rr_async() : NULL);
        if (rr && g_nfrac > 1) { g_nfrac = 1; interp_applied = -1; }
    }
#endif
    if (interp_applied != G.interp_enabled) {
        interp_applied = G.interp_enabled;
        psx_interp_config(interp_applied ? g_nfrac : 0, 2, interp_sink);
    }
    if (hw_applied != MODE_HW(G.gfx_mode)) {
        hw_applied = MODE_HW(G.gfx_mode);
        psx_interp_hw(hw_applied ? hw_sink : NULL, 0);
    }
#ifdef AC3_LIBRE
    /* pantalla ancha: solo mientras el juego dibuja terreno (la mision; menus y videos a 4:3) */
    static uint32_t terrain_last;
    static int terrain_idle = 1000;
    if (ac3_terrain_count != terrain_last) { terrain_last = ac3_terrain_count; terrain_idle = 0; }
    else if (terrain_idle < 1000) terrain_idle++;
    G.wide_now = MODE_WIDE(G.gfx_mode) && terrain_idle < 15;
    psx_gte_wide = G.wide_now ? 0.75f : 1.0f;
    /* etapa 8, paso 2: en la mision con la GPU, la imagen real solo la dibuja la GPU (el software
     * solo interpreta la lista): menos trabajo en el hilo del juego */
    psx_interp_hw_main(MODE_HW(G.gfx_mode) && terrain_idle < 15 && G.dbg_only != 0);
    psx_hw_onepass = G.gfx_mode == 4;
#endif
    /* mando fisico y mando virtual (etapa 7, paso 8): los botones se suman; el stick tactil, si se toca, manda */
    float tx, ty;
    int tact;
    touchpad_stick(&tx, &ty, &tact);
    psx_pad_set(0, (uint16_t)(G.buttons | G.keys | touchpad_buttons()), to_axis(tact ? tx : G.lx), to_axis(tact ? ty : G.ly),
                to_axis(G.rx), to_axis(G.ry));
    static uint32_t img[640 * 512];
    int w, h;
    psx_display_frame(img, 640 * 512, &w, &h);
    pthread_mutex_lock(&G.lock);
    int gw, gh, gon, rgb24;
    psx_display_geom(&gw, &gh, &gon, &rgb24);   /* los videos van en color de 24 bits: a 4:3 */
    if (w && h) { memcpy(G.frame, img, (size_t)w * h * 4); G.fw = w; G.fh = h; G.frame_wide = MODE_WIDE(G.gfx_mode) && !rgb24; }
    pthread_mutex_unlock(&G.lock);
}

static void tick(void) { psx_vblank(); }

/* sumidero de la interpolacion (hilo del juego para la real; hilos de dibujo para las
 * intermedias): hora de salida = hora del paso + fase */
static void interp_sink(const uint32_t *rgba, int w, int h, int num, int den, int vb, double t_step) {
    if (w * h > 640 * 512) return;
    double due = t_step + (double)num / den * vb / (double)g_refresh;
    pthread_mutex_lock(&G.lock);
    int k = 0;   /* la ranura libre o la mas antigua */
    for (int i = 0; i < 8; i++) {
        if (!G.q[i].full) { k = i; break; }
        if (G.q[i].due < G.q[k].due) k = i;
    }
    memcpy(G.q[k].px, rgba, (size_t)w * h * 4);
    G.q[k].w = w; G.q[k].h = h; G.q[k].due = due; G.q[k].full = 1; G.q[k].wide = MODE_WIDE(G.gfx_mode);
    G.q[k].hw = 0; G.q[k].seq = ++G.seq;
    G.interp_last = now();
    pthread_mutex_unlock(&G.lock);
}

/* sumidero de la GPU (modos 0 y 2): copia la lista, sus subidas y la VRAM de partida a una ranura */
static void hw_sink(const PsxHwList *l, const uint16_t *bg, const uint16_t *sw, int x0, int y0, int w, int h, int num, int den,
                    int vb, double t_step) {
    (void)sw;
    double due = t_step + (double)num / den * vb / (double)g_refresh;
    pthread_mutex_lock(&G.lock);
    int k = 0;
    for (int i = 0; i < 8; i++) {
        if (!G.q[i].full) { k = i; break; }
        if (G.q[i].due < G.q[k].due) k = i;
    }
    if (G.q[k].capp < l->n) {
        free(G.q[k].prims);
        G.q[k].capp = l->n + 1024;
        G.q[k].prims = malloc((size_t)G.q[k].capp * sizeof(PsxHwPrim));
    }
    if (G.q[k].capd < l->ndata) {
        free(G.q[k].data);
        G.q[k].capd = l->ndata + 16384;
        G.q[k].data = malloc((size_t)G.q[k].capd * 2);
    }
    if (!G.q[k].bg) G.q[k].bg = malloc(1024 * 512 * 2);
    if (G.q[k].prims && G.q[k].data && G.q[k].bg) {
        memcpy(G.q[k].prims, l->p, (size_t)l->n * sizeof(PsxHwPrim));
        memcpy(G.q[k].data, l->data, (size_t)l->ndata * 2);
        memcpy(G.q[k].bg, bg, 1024 * 512 * 2);
        G.q[k].np = l->n; G.q[k].nd = l->ndata;
        G.q[k].hw = 1; G.q[k].w = w; G.q[k].h = h; G.q[k].x0 = x0; G.q[k].y0 = y0;
        G.q[k].real = num == den; G.q[k].stale = l->main_stale;
        if (now() > due) { if (num == den) g_late_real++; else g_late_inter++; }
        G.q[k].due = due; G.q[k].full = 1; G.q[k].wide = MODE_WIDE(G.gfx_mode); G.q[k].seq = ++G.seq;
        G.interp_last = now();
    } else G.q[k].capp = G.q[k].capd = 0;
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
#ifdef AC3_LIBRE
    LOG("version %s (%d Hz), APK libre: ACE.BIN del disco (%u bytes), interprete", R->name, R->refresh, g_ab.size);
    memcpy(psx_ram + (g_ab.load & 0x1FFFFFu), g_ace, g_ab.size);
    psx_gte_tables_init(g_ace, g_ab.size);
#else
    LOG("version %s (%d Hz)", R->name, R->refresh);
    memcpy(psx_ram + 0x10000, R->image, *R->image_len);
    psx_gte_tables_init(R->image, *R->image_len);
#endif
    psx_pad_connect(0, 0x73);   /* analogico: los sticks de la Retroid */
    psx_pad_connect(1, 0);
    psx_card_attach(0, G.card_path);
    psx_card_detach(1);
    psx_set_refresh(R->refresh);
    psx_audio_connect();        /* audio XA del CD -> entrada de CD de la SPU */
    rc_set_region(R->ovl_base, R->lib_lo, R->lib_hi, R->lib_main, R->gp);
#ifdef AC3_LIBRE
    rc_set_interpreter(mipsi_call);
    ac3_wide_install(R);        /* pantalla ancha: el cono de vision del terreno */
    if (ac3_rr_install(R) || ac3_rr_async_install(R)) LOG("interpolacion nueva: esta version no tiene direcciones");
#else
    rc_register(R->funcs, *R->nfuncs);
    for (int i = 0; i < 5; i++) rc_register_overlay(i + 1, R->ovl[i], *R->novl[i]);
    rc_set_overlay_sigs(R->sig, 5);
#endif
    rc_set_hle(R->hle, *R->nhle);
    rc_gte = (PsxGte *)psx_gte_get_context();
    psx_set_call(rc_host_callback);
    psx_set_frame_wait(frame_wait);
    pin_fast_cores();
    psx_interp_config(g_nfrac, 2, interp_sink);   /* luego se ajusta solo (frame_wait) */
    psx_interp_threads(3, pin_fast_cores);
    rc_set_tick(tick, BUDGET_PER_FRAME);
    memset(psx_ram + (R->bss_start & 0x1FFFFFu), 0, R->bss_end - R->bss_start);   /* BSS, como ac3_start */
    RCpu c;
    memset(&c, 0, sizeof c);
    c.r[28] = R->gp;
    c.r[29] = 0x801FFF00u;
    c.r[31] = 0x8000FFF0u;
    rc_ret = c.r[31];
    LOG("arranca ac3_main");
    rc_call(&c, R->main);   /* no vuelve */
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
    if (psx_hw_init()) LOG("GPU: el dibujo por GLES no arranca");
    psx_hw_dither(1);   /* escala 1: el tramado de la PS1, la misma imagen que por software */
    touchpad_set_screen(G.w, G.h, AConfiguration_getDensity(G.app->config));
    LOG("EGL %dx%d, %d dpi", G.w, G.h, AConfiguration_getDensity(G.app->config));
}

static void egl_term(void) {
    if (G.dpy == EGL_NO_DISPLAY) return;
    psx_present_shutdown();
    psx_hw_shutdown();
    touchpad_gl_lost();
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
    if (touchpad_event(e)) return 1;   /* la pantalla tactil: el mando virtual */
    if (type == AINPUT_EVENT_TYPE_MOTION && (AInputEvent_getSource(e) & AINPUT_SOURCE_JOYSTICK)) {
        touchpad_physical();   /* (modo automatico: con un mando fisico se oculta) */
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
        touchpad_physical();
        if (AKeyEvent_getAction(e) == AKEY_EVENT_ACTION_DOWN) {
            G.keys |= m;
            /* L3+R3: modo de imagen (60 fps + 16:9 -> 60 fps + 4:3 -> el original) */
            if ((G.keys & (PSX_PAD_L3 | PSX_PAD_R3)) == (PSX_PAD_L3 | PSX_PAD_R3) && AKeyEvent_getRepeatCount(e) == 0) {
                /* 16:9 -> 4:3 -> original (1, software a 16:9, y 4-6, de medida: ya no estan en el ciclo) */
                static const int next[7] = {2, 2, 3, 0, 0, 0, 0};
                G.gfx_mode = next[G.gfx_mode];
                LOG("modo de imagen: %s", G.gfx_mode == 0 ? "GPU, 60 fps, 16:9" : G.gfx_mode == 1 ? "software, 60 fps, 16:9"
                                          : G.gfx_mode == 2 ? "GPU, 60 fps, 4:3" : G.gfx_mode == 3 ? "original"
                                          : G.gfx_mode == 4 ? "GPU 1P (medida: sustractivas en una pasada)" : "GPU, medida");
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
    {':', {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}},
    {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}},
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

/* la version del disco elegido, por su ejecutable de arranque (SYSTEM.CNF), entre las que lleva
 * el APK; NULL si no es ninguna */
#ifdef AC3_LIBRE
static const Ac3Version *disc_region(void) {
    const Ac3Version *v = ac3_version_detect();
    if (!v) return NULL;
    int r = psx_acebin_from_disc(g_ace, sizeof g_ace, &g_ab);
    if (r) { LOG("ACE.BIN del disco: fallo %d", r); return NULL; }
    return v;
}
#else
static const Ac3Region *disc_region(void) {
    const Ac3Region *all[2] = {&ac3_region_eu, &ac3_region_us};
    for (int i = 0; i < 2; i++) {
        CdlFILE f;
        if (all[i] && CdSearchFile(&f, (char *)all[i]->boot_exe)) return all[i];
    }
    return NULL;
}
#endif

/* MainActivity.pickDisc() desde el hilo principal */
static void ask_disc(void) {
    JavaVM *vm = G.app->activity->vm;
    JNIEnv *env;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return;
    jobject act = G.app->activity->clazz;
    jclass cl = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cl, "pickDisc", "()V");
    if (m) (*env)->CallVoidMethod(env, act, m);
    (*env)->DeleteLocalRef(env, cl);
    (*vm)->DetachCurrentThread(vm);
}

void android_main(struct android_app *app) {
    G.app = app;
    G.dpy = EGL_NO_DISPLAY;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    snprintf(G.card_path, sizeof G.card_path, "%s/tarjeta1.mcd", app->activity->internalDataPath);
    if (app->activity->externalDataPath) {
        mkdir(app->activity->externalDataPath, 0770);
        snprintf(G.dbg_path, sizeof G.dbg_path, "%s/depurar.txt", app->activity->externalDataPath);
    }
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
            int ok = psx_cd_open_fd(fd) == 0;
            if (ok) R = disc_region();
            if (ok && !R) {
                /* ningun ejecutable de las versiones que lleva el APK: se pide otro disco */
                psx_cd_close();
                LOG("el disco no es de ninguna version de este APK: se pide otro");
                ask_disc();
            } else if (ok) {
                g_refresh = R->refresh;
                g_spf = 44100 / g_refresh;
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
        int best = -1, sw = 0, sh = 0, wide = 0;   /* interpolacion activa: la ultima imagen cuya hora ya llego */
        /* las imagenes reales de la GPU que se saltan se dibujan igual (sin mostrarlas): la GPU
         * conserva cada imagen y la siguiente parte de ella (etapa 8, paso 2) */
        static PsxHwList rl[8];
        static uint16_t *rbg[8];
        static int rgeo[8][4];
        static double rdue[8];
        int nr = 0;
        if (G.interp_enabled && t - G.interp_last < 0.15) {
            /* ritmo (paso 3b): la pendiente mas antigua (ya vencida y aun no mostrada); con 3 o mas
             * atrasadas, de las dos mas nuevas. Antes era la mas reciente y se tiraban las demas: con la
             * vuelta despertada por la GPU (no por el refresco), una intermedia y su real vencian a
             * veces en la misma vuelta y la intermedia se perdia aunque llegara a tiempo */
            static double last_due = -1;
            int pend[8], npend = 0;
            for (int i = 0; i < 8; i++)
                if (G.q[i].full && G.q[i].due <= t && G.q[i].due > last_due) pend[npend++] = i;
            for (int a = 0; a < npend; a++)   /* por hora */
                for (int b = a + 1; b < npend; b++)
                    if (G.q[pend[b]].due < G.q[pend[a]].due) { int x = pend[a]; pend[a] = pend[b]; pend[b] = x; }
            if (npend) best = pend[npend >= 3 ? npend - 2 : 0];
            else   /* nada nuevo: la que ya se muestra */
                for (int i = 0; i < 8; i++)
                    if (G.q[i].full && G.q[i].due == last_due) best = i;
            if (best >= 0) last_due = G.q[best].due;
            if (best >= 0)
                for (int i = 0; i < 8; i++)
                    if (G.q[i].full && G.q[i].due < G.q[best].due) {
                        /* (la que ya se dibujo como la que tocaba tiene np = 0: no se repite) */
                        if (G.q[i].hw && G.q[i].real && G.q[i].np > 0) {   /* sus buferes se intercambian con los de rl[nr] */
                            PsxHwPrim *tp = rl[nr].p; int tcp = rl[nr].max;
                            uint16_t *td = rl[nr].data; int tcd = rl[nr].maxdata;
                            uint16_t *tb = rbg[nr];
                            rl[nr].p = G.q[i].prims; rl[nr].max = G.q[i].capp;
                            rl[nr].data = G.q[i].data; rl[nr].maxdata = G.q[i].capd;
                            rl[nr].n = G.q[i].np; rl[nr].ndata = G.q[i].nd; rl[nr].main_stale = G.q[i].stale;
                            rbg[nr] = G.q[i].bg;
                            rgeo[nr][0] = G.q[i].x0; rgeo[nr][1] = G.q[i].y0; rgeo[nr][2] = G.q[i].w; rgeo[nr][3] = G.q[i].h;
                            rdue[nr] = G.q[i].due;
                            G.q[i].prims = tp; G.q[i].capp = tcp; G.q[i].data = td; G.q[i].capd = tcd; G.q[i].bg = tb;
                            G.q[i].np = G.q[i].nd = 0;
                            nr++;
                        }
                        else if (G.q[i].hw && !G.q[i].real && G.q[i].np > 0) g_drop_inter++;
                        G.q[i].full = 0;
                    }
        }
        /* GPU: los buferes de la ranura se intercambian (sin copiar) y se dibujan fuera del cerrojo */
        static PsxHwList hl;
        static uint16_t *hbg;
        static int hx0, hy0, hw_w, hw_h, hw_real;
        static uint32_t hw_seq, last_seq;
        int hw = 0;
        uint32_t seq = 0;
        if (best >= 0 && G.q[best].hw) {
            hw = 1; wide = G.q[best].wide; seq = G.q[best].seq;
            if (seq != hw_seq) {
                PsxHwPrim *tp = hl.p; int tcp = hl.max;
                uint16_t *td = hl.data; int tcd = hl.maxdata;
                uint16_t *tb = hbg;
                hl.p = G.q[best].prims; hl.max = G.q[best].capp;
                hl.data = G.q[best].data; hl.maxdata = G.q[best].capd;
                hbg = G.q[best].bg;
                hl.n = G.q[best].np; hl.ndata = G.q[best].nd; hl.main_stale = G.q[best].stale; hw_real = G.q[best].real;
                hx0 = G.q[best].x0; hy0 = G.q[best].y0; hw_w = G.q[best].w; hw_h = G.q[best].h;
                G.q[best].prims = tp; G.q[best].capp = tcp;
                G.q[best].data = td; G.q[best].capd = tcd;
                G.q[best].bg = tb;
                G.q[best].np = G.q[best].nd = 0;
            }
        } else if (best >= 0) { sw = G.q[best].w; sh = G.q[best].h; wide = G.q[best].wide; seq = G.q[best].seq; memcpy(G.show, G.q[best].px, (size_t)sw * sh * 4); }
        else if (G.fw && G.fh) { sw = G.fw; sh = G.fh; wide = G.frame_wide; memcpy(G.show, G.frame, (size_t)sw * sh * 4); }
        pthread_mutex_unlock(&G.lock);
        /* contador: imagenes distintas por segundo y velocidad del juego (VBlank por segundo) */
        static double c_t0;
        static uint32_t c_last, c_vb0;
        static int c_new, c_fps, c_vel;
        static char line1[48], line2[48], line3[48];
        static double c_hw_ms, c_swap_ms;   /* GPU: ms de CPU en el dibujo y en eglSwapBuffers, por imagen */
        /* medicion (paso 3): vueltas de este bucle (una por refresco si va bien), la mas larga, las de
         * mas de 20 ms, imagenes reales saltadas que se dibujan igual, y el tiempo de la GPU de una
         * imagen por segundo (glFinish) */
        static int c_loops, c_slow, c_skipped;
        static double c_loop_max, c_last_loop, c_gpu_ms = -1;
        static int c_measure;
        static int c_hw_n;
        if (seq) { if (seq != last_seq) { c_new++; last_seq = seq; } }
        else if (sw && sh) {
            uint32_t hsh = frame_hash(G.show, sw, sh);
            if (hsh != c_last) { c_new++; c_last = hsh; }
        }
        if (c_t0 == 0) { c_t0 = t; c_vb0 = (uint32_t)VSync(-1); }
        if (t - c_t0 >= 1.0) {
            uint32_t vb = (uint32_t)VSync(-1);
            c_fps = (int)lrint(c_new / (t - c_t0));
            c_vel = (int)lrint((vb - c_vb0) / (t - c_t0) * 100.0 / g_refresh);   /* % de 50 (o 60) VBlank/s */
            snprintf(line1, sizeof line1, "FPS %d  VEL %d%%", c_fps, c_vel);
            static const char *mds[7] = {"16:9", "SW 16:9", "4:3", "ORIG", "GPU 1P", "GPU ADEL", "GPU SM"};
            const char *md = mds[G.gfx_mode];
            if (G.st_nfrac) snprintf(line2, sizeof line2, "INT %d  CPU %d%%  %s", G.st_nfrac, G.st_cpu, md);
            else snprintf(line2, sizeof line2, "INT -  CPU %d%%  %s", G.st_cpu, md);
            if (G.running)
                LOG("fps %d, velocidad %d%%, intermedias %d, hilo del juego %d%% (cpu%d), pasos %d (saltados %d, sin parejas %d), "
                    "ms por paso: juego %.1f, hilo de dibujo 0 %.1f (redibujo %.1f); modo %s", c_fps, c_vel, G.st_nfrac, G.st_cpu,
                    G.st_core, G.st_steps, G.st_skip, G.st_unpaired, G.st_real_ms, G.st_worker_ms, G.st_rr_ms, md);
            extern int psx_hw_stat_draws, psx_hw_stat_uploads;
            if (c_hw_n) {
                snprintf(line3, sizeof line3, "GL %.1f  SWAP %.1f", c_hw_ms / c_hw_n, c_swap_ms / c_hw_n);
                LOG("GPU x%d: %d imagenes, %.1f ms de dibujo y %.1f de eglSwapBuffers por imagen; %d llamadas y %d subidas por imagen; "
                    "cortes por imagen: recorte %d, mezcla %d, pasada %d, mascara %d",
                    G.scale, c_hw_n, c_hw_ms / c_hw_n, c_swap_ms / c_hw_n, psx_hw_stat_draws / c_hw_n, psx_hw_stat_uploads / c_hw_n,
                    psx_hw_stat_cut[0] / c_hw_n, psx_hw_stat_cut[1] / c_hw_n, psx_hw_stat_cut[2] / c_hw_n, psx_hw_stat_cut[3] / c_hw_n);
                memset(psx_hw_stat_cut, 0, sizeof psx_hw_stat_cut);
            } else line3[0] = 0;
            c_hw_ms = c_swap_ms = 0; c_hw_n = 0;
            psx_hw_stat_draws = psx_hw_stat_uploads = 0;
            if (G.running)
                LOG("presentacion: %d vueltas, la mas larga %.1f ms, %d de mas de 20 ms; reales saltadas dibujadas %d; GPU de una imagen %.1f ms; "
                    "llegan tarde: reales %d, intermedias %d; intermedias tiradas %d; hilo de dibujo, el peor %.1f ms",
                    c_loops, c_loop_max, c_slow, c_skipped, c_gpu_ms, g_late_real, g_late_inter, g_drop_inter, g_worker_max);
            g_late_real = g_late_inter = g_drop_inter = 0;
            c_loops = c_slow = c_skipped = 0; c_loop_max = 0; c_gpu_ms = -1; c_measure = 1;
            c_t0 = t; c_vb0 = vb; c_new = 0;
        }
        int hw_drawn = 0;
        for (int a = 0; a < nr; a++)   /* las reales saltadas, en orden */
            for (int b = a + 1; b < nr; b++)
                if (rdue[b] < rdue[a]) {
                    PsxHwList tl = rl[a]; rl[a] = rl[b]; rl[b] = tl;
                    uint16_t *tb = rbg[a]; rbg[a] = rbg[b]; rbg[b] = tb;
                    int tg[4]; memcpy(tg, rgeo[a], sizeof tg); memcpy(rgeo[a], rgeo[b], sizeof tg); memcpy(rgeo[b], tg, sizeof tg);
                    double td = rdue[a]; rdue[a] = rdue[b]; rdue[b] = td;
                }
        /* alta resolucion (etapa 8, paso 4): a mas de 1x, color de 24 bits sin tramado (degradados suaves) */
        static double dbg_t;
        if (t - dbg_t > 1.0) { dbg_t = t; dbg_read(); }
        int scale = G.dbg_scale >= 1 && G.dbg_scale <= 4 ? G.dbg_scale : G.scale;
        psx_hw_truecolor(G.dbg_true >= 0 ? G.dbg_true : scale > 1);
        psx_hw_filter(G.dbg_filter >= 0 ? G.dbg_filter : G.filter);
        for (int a = 0; a < nr; a++) psx_hw_frame_ex(&rl[a], rbg[a], rgeo[a][0], rgeo[a][1], rgeo[a][2], rgeo[a][3], scale, 1);
        c_skipped += nr;
        if (hw) {
            if (seq != hw_seq) {
                double a = now();
                psx_hw_frame_ex(&hl, hbg, hx0, hy0, hw_w, hw_h, scale, hw_real);
                if (c_measure) { c_measure = 0; double g0 = now(); glFinish(); c_gpu_ms = (now() - a) * 1000; (void)g0; }
                c_hw_ms += (now() - a) * 1000; c_hw_n++; hw_drawn = 1;
                hw_seq = seq;
            }
            int tw, th;
            unsigned tex = psx_hw_texture(&tw, &th);
            psx_present_texture(tex, tw, th, G.w, G.h, wide ? PSX_FIT_169 : PSX_FIT_43, scale > 1);
            if (G.show_stats && line1[0]) {   /* el contador encima, como textura con transparencia */
                static uint32_t ov[128 * 29];
                memset(ov, 0, sizeof ov);
                draw_text(ov, 128, 29, 1, 1, line1);
                draw_text(ov, 128, 29, 1, 10, line2);
                /* (la tercera linea, GL y SWAP, solo va al registro) */
                psx_present_overlay(ov, 128, 29, hw_w, hw_h);
            }
        } else if (sw && sh) {
            if (G.show_stats && line1[0]) { draw_text(G.show, sw, sh, 4, 4, line1); draw_text(G.show, sw, sh, 4, 13, line2); }
            psx_present_frame(G.show, sw, sh, G.w, G.h, wide ? PSX_FIT_169 : PSX_FIT_43, 0);
        } else { glViewport(0, 0, G.w, G.h); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); }
        touchpad_draw();   /* el mando virtual, encima de todo */
        double sa = now();
        eglSwapBuffers(G.dpy, G.surf);
        if (hw_drawn) c_swap_ms += (now() - sa) * 1000;
        {
            double tl = now();
            if (c_last_loop > 0) {
                double d = (tl - c_last_loop) * 1000;
                c_loops++;
                if (d > c_loop_max) c_loop_max = d;
                if (d > 20) c_slow++;
            }
            c_last_loop = tl;
        }
    }
}
