/* Arranque NATIVO de Ace Combat 3 en el PC (etapa 6, paso 4): el codigo recompilado
 * (data/recomp/*.c, generado desde el disco del usuario) enlazado con la capa psx/.
 *   ac3_native <imagen.bin> <ace_eu.bin> <fotogramas> [dir_capturas] [cada_n] [guion]
 * Guion del mando: "fotograma:boton[+boton]:duracion,..." como tools/recomp/boot.py.
 * Sin ventana: guarda capturas PPM de la pantalla (psx_display) y mide la velocidad.
 * El mismo arranque, ejecutado por el interprete con el codigo original, es la referencia
 * (tools/recomp/boot.py). */
#include <execinfo.h>
#include <math.h>
#include <setjmp.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "psx_display.h"
#include "psx_libapi.h"
#include "psx_libcard.h"
#include "psx_libcd.h"
#include "psx_libetc.h"
#include "psx_libgte.h"
#include "psx_interp.h"
#include "psx_libpad.h"
#include "psx_mem.h"
#include "recomp_rt.h"

extern const RcFunc rc_funcs_ace_eu[], rc_funcs_overlay1[], rc_funcs_overlay2[], rc_funcs_overlay3[],
    rc_funcs_overlay4[], rc_funcs_overlay5[];
extern const unsigned rc_nfuncs_ace_eu, rc_nfuncs_overlay1, rc_nfuncs_overlay2, rc_nfuncs_overlay3,
    rc_nfuncs_overlay4, rc_nfuncs_overlay5;
extern const RcFunc hle_table[];
extern const unsigned hle_count;
extern const uint8_t ovl_sig[5][64];
void rc_register(const RcFunc *funcs, unsigned n);
void rc_register_overlay(int n, const RcFunc *funcs, unsigned count);

#define AC3_MAIN 0x80025D08u
/* presupuesto por fotograma: unidades = entradas a funcion + saltos hacia atras. Calibrado
 * para que el juego avance como en el interprete (450.000 instrucciones por fotograma). */
#define BUDGET_PER_FRAME 25000

static struct { int frame; uint16_t mask; int dur; } script[64];
static int nscript, max_frames, every, last_vcount;
static const char *shots;
static jmp_buf stop;

static void save_screen(int frame) {
    static uint32_t img[640 * 512];
    int w, h;
    psx_display_frame(img, 640 * 512, &w, &h);
    if (!w || !h) return;
    char path[512];
    snprintf(path, sizeof path, "%s/f%05d.ppm", shots, frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(&img[i], 1, 3, f);
    fclose(f);
}

/* cada fotograma nuevo (lo avance el reloj o VSync): mando, capturas, parada */
static void frame_check(void) {
    int v = VSync(-1);
    while (last_vcount < v) {
        last_vcount++;
        uint16_t mask = 0;
        for (int i = 0; i < nscript; i++)
            if (last_vcount >= script[i].frame && last_vcount < script[i].frame + script[i].dur) mask |= script[i].mask;
        psx_pad_set(0, mask, 0x80, 0x80, 0x80, 0x80);
        if (getenv("AC3_PADDBG") && mask) {   /* depuracion: lo que ve el juego en su bufer de mando */
            uint32_t pb = *(uint32_t *)psx_ptr(0x800BAA04u);
            const uint8_t *b = psx_ptr(pb);
            printf("fotograma %d: mando %04x, bufer del juego (%08x) %02x %02x %02x %02x\n", last_vcount, mask, pb, b[0], b[1], b[2], b[3]);
        }
        if (shots && every && last_vcount % every == 0) save_screen(last_vcount);
        if (last_vcount >= max_frames) longjmp(stop, 1);
    }
}

/* prueba de la interpolacion (AC3_INTERP=n): cada 100 fotogramas, estadisticas; entre
 * AC3_INTERP_FROM y +8 fotogramas, todas las imagenes (intermedias y reales) a las capturas */
static int interp_from;
/* prueba con verdad conocida (AC3_INTERP_TEST=1): PSNR de la intermedia de k y k+2 frente al
 * k+1 real, y de los vecinos reales frente al k+1 (lo que se veria sin interpolar) */
static uint32_t t_prev[640 * 512], t_mid[640 * 512], t_int[640 * 512];
static int t_have_prev, t_have_mid, t_n, t_fresh;
static double t_sum_int, t_sum_prev, t_sum_next, t_min_gain = 1e9;
static double psnr(const uint32_t *a, const uint32_t *b, int n) {
    double e = 0;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 24; k += 8) { int d = (int)((a[i] >> k) & 255) - (int)((b[i] >> k) & 255); e += d * d; }
    e /= 3.0 * n;
    return e < 1e-9 ? 99 : 10 * log10(255.0 * 255.0 / e);
}
static void interp_test(const uint32_t *rgba, int w, int h, int num, int den) {
    int n = w * h;
    if (n > 640 * 512) return;
    if (den == 0) { memcpy(t_mid, rgba, n * 4); t_have_mid = 1; return; }
    if (num < den) { memcpy(t_int, rgba, n * 4); t_fresh = 1; return; }
    if (t_have_prev && t_have_mid && den == 2 && t_fresh) {
        double pi = psnr(t_int, t_mid, n), pp = psnr(t_prev, t_mid, n), pn = psnr(rgba, t_mid, n);
        t_sum_int += pi; t_sum_prev += pp; t_sum_next += pn; t_n++;
        double gain = pi - (pp > pn ? pp : pn);
        if (gain < t_min_gain) t_min_gain = gain;
        if (getenv("AC3_INTERP_TEST_DUMP") && shots && abs(last_vcount - atoi(getenv("AC3_INTERP_TEST_DUMP"))) <= 1) {
            const uint32_t *im[4] = {t_prev, t_mid, t_int, rgba};
            const char *nm[4] = {"0anterior", "1real_intermedio", "2interpolada", "3siguiente"};
            for (int q = 0; q < 4; q++) {
                char path[512];
                snprintf(path, sizeof path, "%s/caso%05d_%s.ppm", shots, last_vcount, nm[q]);
                FILE *f = fopen(path, "wb");
                if (!f) continue;
                fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (int i = 0; i < n; i++) fwrite(&im[q][i], 1, 3, f);
                fclose(f);
            }
        }
        if (getenv("AC3_INTERP_TEST_LOG")) {
            int a, b, c, d, e;
            psx_interp_stats(&a, &b, &c, &d, &e);
            printf("caso f%d: interpolada %.2f anterior %.2f siguiente %.2f (gte %d, de la lista %d, emparejadas %d, media %d)\n",
                   last_vcount, pi, pp, pn, a, b, c, d);
        }
    }
    memcpy(t_prev, rgba, n * 4);
    t_fresh = 0;
    t_have_prev = 1;
    t_have_mid = 0;
}

/* cada paso se identifica por su hora (t_step): con hilos de dibujo (AC3_INTERP_THREADS) las
 * intermedias llegan despues de la real y desde otros hilos; el numero de paso se asigna la
 * primera vez que se ve (siempre desde el hilo del juego, en DrawOTag), asi los nombres de las
 * capturas coinciden con y sin hilos */
static pthread_mutex_t sink_mu = PTHREAD_MUTEX_INITIALIZER;

static void interp_sink(const uint32_t *rgba, int w, int h, int num, int den, int vb, double t_step) {
    if (psx_interp_skip_test && last_vcount > 3500) interp_test(rgba, w, h, num, den);
    if (den == 0) return;
    static int last_stat;
    if (num == den && last_vcount - last_stat >= 100) {
        last_stat = last_vcount;
        int a, b, c, d, e;
        psx_interp_stats(&a, &b, &c, &d, &e);
        printf("interp f%d: GTE %d, lista %d coordenadas: %d de la GTE, %d emparejadas, %d por la media (paso %d VBlank)\n",
               last_vcount, a, e, b, c, d, vb);
    }
    if (!shots || !interp_from) return;
    static double seen_t[8];
    static int seen_step[8], seen_save[8], nsteps;
    pthread_mutex_lock(&sink_mu);
    int k = -1;
    for (int i = 0; i < 8; i++) if (seen_t[i] == t_step) k = i;
    if (k < 0) {
        k = nsteps & 7;
        seen_t[k] = t_step;
        seen_step[k] = nsteps++;
        seen_save[k] = last_vcount >= interp_from && last_vcount < interp_from + 8;
    }
    int step = seen_step[k], save = seen_save[k];
    pthread_mutex_unlock(&sink_mu);
    if (!save) return;
    char path[512];
    snprintf(path, sizeof path, "%s/i%05d_%d-%d.ppm", shots, step, num, den);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(&rgba[i], 1, 3, f);
    fclose(f);
}

static void tick(void) {
    psx_vblank();
    frame_check();
}

static void on_vsync_frame(void) { frame_check(); }

/* vigia (AC3_WATCH=segundos): si el juego no termina, imprime la pila de llamadas (las
 * funciones recompiladas se llaman f_<direccion>) y sale */
static void watch(int sig) {
    (void)sig;
    void *bt[64];
    int n = backtrace(bt, 64);
    dprintf(2, "vigia: fotograma %d, pila:\n", last_vcount);
    backtrace_symbols_fd(bt, n, 2);
    _exit(3);
}

static uint16_t button(const char *b) {
    static const char *names[16] = {"select", "l3", "r3", "start", "arriba", "derecha", "abajo", "izquierda",
                                    "l2", "r2", "l1", "r1", "triangulo", "circulo", "cruz", "cuadrado"};
    for (int i = 0; i < 16; i++) if (!strcmp(names[i], b)) return (uint16_t)(1u << i);
    return 0;
}

static void parse_script(const char *s) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", s);
    char *sp1, *sp2;
    for (char *item = strtok_r(buf, ",", &sp1); item && nscript < 64; item = strtok_r(NULL, ",", &sp1)) {
        char btns[128];
        int f, d;
        if (sscanf(item, "%d:%127[^:]:%d", &f, btns, &d) != 3) continue;
        uint16_t m = 0;
        for (char *b = strtok_r(btns, "+", &sp2); b; b = strtok_r(NULL, "+", &sp2)) m |= button(b);
        script[nscript].frame = f; script[nscript].mask = m; script[nscript].dur = d;
        nscript++;
    }
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "uso: %s <imagen> <ace_eu.bin> <fotogramas> [capturas] [cada_n] [guion]\n", argv[0]); return 2; }
    max_frames = atoi(argv[3]);
    shots = argc > 4 ? argv[4] : NULL;
    every = argc > 5 ? atoi(argv[5]) : 100;
    if (argc > 6) parse_script(argv[6]);
    /* ACE.BIN en la RAM (0x80010000) y tablas de la GTE */
    FILE *f = fopen(argv[2], "rb");
    if (!f) { puts("no se abre ACE.BIN"); return 1; }
    static uint8_t ace[0x200000];
    size_t n = fread(ace, 1, sizeof ace, f);
    fclose(f);
    memcpy(psx_ram + 0x10000, ace, n);
    psx_gte_tables_init(ace, n);
    if (psx_cd_open(argv[1])) { puts("no se abre la imagen"); return 1; }
    psx_pad_connect(0, getenv("AC3_PAD_ID") ? (int)strtol(getenv("AC3_PAD_ID"), NULL, 16) : 0x41);   /* 73 = analogico */
    psx_pad_connect(1, 0);
    psx_card_attach(0, "/tmp/ac3_native_tarjeta1.mcd");
    psx_card_detach(1);
    psx_set_refresh(50);
    /* codigo recompilado: residente, overlays (deteccion por firma) y enlace con psx/ */
    rc_register(rc_funcs_ace_eu, rc_nfuncs_ace_eu);
    rc_register_overlay(1, rc_funcs_overlay1, rc_nfuncs_overlay1);
    rc_register_overlay(2, rc_funcs_overlay2, rc_nfuncs_overlay2);
    rc_register_overlay(3, rc_funcs_overlay3, rc_nfuncs_overlay3);
    rc_register_overlay(4, rc_funcs_overlay4, rc_nfuncs_overlay4);
    rc_register_overlay(5, rc_funcs_overlay5, rc_nfuncs_overlay5);
    rc_set_overlay_sigs(ovl_sig, 5);
    rc_set_hle(hle_table, hle_count);
    rc_gte = psx_gte_get_context();
    psx_set_call(rc_host_callback);
    rc_set_tick(tick, BUDGET_PER_FRAME);
    rc_set_frame_hook(on_vsync_frame);
    if (getenv("AC3_INTERP")) {
        interp_from = getenv("AC3_INTERP_FROM") ? atoi(getenv("AC3_INTERP_FROM")) : 0;
        psx_interp_skip_test = getenv("AC3_INTERP_TEST") != NULL;
        psx_interp_config(atoi(getenv("AC3_INTERP")), psx_interp_skip_test ? 1 : 2, interp_sink);
        if (getenv("AC3_INTERP_THREADS")) psx_interp_threads(atoi(getenv("AC3_INTERP_THREADS")), NULL);
    }
    /* como ac3_start: BSS a cero y ac3_main */
    memset(psx_ram + 0xBACB0, 0, 0xC9DC0 - 0xBACB0);
    RCpu c;
    memset(&c, 0, sizeof c);
    c.r[28] = 0x800BA9F4u;
    c.r[29] = 0x801FFF00u;
    c.r[31] = 0x8000FFF0u;
    rc_ret = c.r[31];
    if (getenv("AC3_WATCH")) { signal(SIGALRM, watch); alarm((unsigned)atoi(getenv("AC3_WATCH"))); }
    clock_t t0 = clock();
    if (!setjmp(stop)) rc_call(&c, AC3_MAIN);
    double dt = (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (getenv("AC3_INTERP")) {
        PsxInterpPerf pf;
        psx_interp_perf(&pf, 0);
        printf("interpolacion: %d pasos con intermedias, %d sin bastantes parejas, %d saltados (hilos ocupados)\n",
               pf.steps, pf.unpaired, pf.skipped);
    }
    if (t_n) printf("prueba de interpolacion (%d casos): PSNR frente al paso intermedio real: interpolada %.2f dB, "
                    "anterior real %.2f dB, siguiente real %.2f dB; peor ganancia %.2f dB\n", t_n, t_sum_int / t_n,
                    t_sum_prev / t_n, t_sum_next / t_n, t_min_gain);
    printf("%d fotogramas en %.2f s de CPU (%.0f fotogramas/s; tiempo real del juego: %.1f s a 50 Hz)\n", last_vcount, dt,
           last_vcount / (dt > 0 ? dt : 1), last_vcount / 50.0);
    return 0;
}
