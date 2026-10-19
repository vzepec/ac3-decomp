/* Arranque NATIVO de Ace Combat 3 en el PC (etapa 6, paso 4): el codigo recompilado
 * (data/recomp/*.c, generado desde el disco del usuario) enlazado con la capa psx/.
 *   ac3_native <imagen.bin> <ace_eu.bin|ace_us.bin> <fotogramas> [dir_capturas] [cada_n] [guion]
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

#ifdef AC3_LIBRE
/* etapa 7, modo libre: solo codigo del repositorio. La version se detecta en el disco, ACE.BIN
 * se lee de el y todo lo ejecuta el interprete (make libre) */
#include "ac3_versions.h"
#include "psx_gte.h"
#include "psx_acebin.h"
static const Ac3Version *V;
#define AC3_MAIN (V->main)
#define AC3_GP (V->gp)
#define AC3_BSS_START (V->bss_start)
#define AC3_BSS_END (V->bss_end)
#define AC3_OVL_BASE (V->ovl_base)
#define AC3_LIB_LO (V->lib_lo)
#define AC3_LIB_HI (V->lib_hi)
#define AC3_LIB_MAIN (V->lib_main)
#define AC3_REFRESH (V->refresh)
#define AC3_PAD_BUF (V->pad_buf)
#else
/* direcciones de la version: por defecto las del ACE.BIN europeo; otra version las trae en una
 * cabecera generada por tools/recomp/region.py (-include data/recomp_us/ac3_region.h) */
#ifndef AC3_MAIN
#define AC3_MAIN 0x80025D08u
#define AC3_GP 0x800BA9F4u
#define AC3_BSS_START 0x800BACB0u
#define AC3_BSS_END 0x800C9DC0u
#define AC3_OVL_BASE 0x800C9DC0u
#define AC3_LIB_LO 0x80099DE0u
#define AC3_LIB_HI 0x800AE09Cu
#define AC3_LIB_MAIN 0x80025214u
#define AC3_REFRESH 50
#define AC3_PAD_BUF 0x800BAA04u
#define AC3_RC_FUNCS rc_funcs_ace_eu
#define AC3_RC_NFUNCS rc_nfuncs_ace_eu
#endif

extern const RcFunc AC3_RC_FUNCS[], rc_funcs_overlay1[], rc_funcs_overlay2[], rc_funcs_overlay3[],
    rc_funcs_overlay4[], rc_funcs_overlay5[];
extern const unsigned AC3_RC_NFUNCS, rc_nfuncs_overlay1, rc_nfuncs_overlay2, rc_nfuncs_overlay3,
    rc_nfuncs_overlay4, rc_nfuncs_overlay5;
extern const RcFunc hle_table[];
extern const unsigned hle_count;
extern const uint8_t ovl_sig[5][64];
#endif
void rc_register(const RcFunc *funcs, unsigned n);
void rc_register_overlay(int n, const RcFunc *funcs, unsigned count);

/* presupuesto por fotograma: unidades = entradas a funcion + saltos hacia atras. Calibrado
 * para que el juego avance como en el interprete (450.000 instrucciones por fotograma). */
#define BUDGET_PER_FRAME 25000

static struct { int frame; uint32_t mask; int dur; } script[64];   /* bits 16-23: palancas */
static int nscript, max_frames, every, last_vcount;
static const char *shots;
static jmp_buf stop;

static void save_screen(int frame) {
    static uint32_t img[640 * 512];
    int w, h;
    psx_display_frame(img, 640 * 512, &w, &h);
    if (!w || !h) return;
    if (getenv("AC3_GEOM_DBG")) {   /* depuracion: modo de pantalla de cada captura */
        int gw, gh, on, rgb24;
        psx_display_geom(&gw, &gh, &on, &rgb24);
        printf("captura %d: %dx%d, color de %d bits\n", frame, gw, gh, rgb24 ? 24 : 15);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/f%05d.ppm", shots, frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(&img[i], 1, 3, f);
    fclose(f);
}

/* cada fotograma nuevo (lo avance el reloj o VSync): mando, capturas, parada */
#ifdef AC3_LIBRE
/* AC3_WIDE=1: pantalla ancha (paso 16) mientras el juego dibuja terreno (la mision): la GTE
 * comprime la x a 3/4. Las capturas salen comprimidas: hay que ensancharlas a 16:9 para verlas */
static int g_wide;
static void wide_update(void) {
    static uint32_t last;
    static int idle = 1000;
    if (!g_wide) return;
    if (ac3_terrain_count != last) { last = ac3_terrain_count; idle = 0; }
    else if (idle < 1000) idle++;
    psx_gte_wide = idle < 15 ? 0.75f : 1.0f;
}
#endif

/* AC3_PACE=hz: a ritmo real (un VBlank cada 1/hz s, como en el aparato), para medir lo que hacen
 * los hilos de dibujo con el tiempo que tendrian */
static void pace(void) {
    static double hz = -1, t0;
    if (hz < 0) hz = getenv("AC3_PACE") ? atof(getenv("AC3_PACE")) : 0;
    if (hz <= 0) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = ts.tv_sec + ts.tv_nsec * 1e-9;
    if (t0 == 0) t0 = t - last_vcount / hz;
    double due = t0 + last_vcount / hz;
    if (due > t) { ts.tv_sec = (time_t)(due - t); ts.tv_nsec = (long)((due - t - (double)ts.tv_sec) * 1e9); nanosleep(&ts, NULL); }
}
static void frame_check(void) {
    int v = VSync(-1);
    while (last_vcount < v) {
        last_vcount++;
        pace();
        if (getenv("AC3_WATCHREAD")) {   /* depuracion: desde donde se lee una direccion (desde AC3_WATCHREAD_FROM) */
            extern uint32_t mipsi_watch_read;
            int from = getenv("AC3_WATCHREAD_FROM") ? atoi(getenv("AC3_WATCHREAD_FROM")) : 0;
            if (last_vcount == from) mipsi_watch_read = (uint32_t)strtoul(getenv("AC3_WATCHREAD"), NULL, 16);
        }
#ifdef AC3_LIBRE
        wide_update();
#endif
        uint32_t all = 0;
        for (int i = 0; i < nscript; i++)
            if (last_vcount >= script[i].frame && last_vcount < script[i].frame + script[i].dur) all |= script[i].mask;
        uint16_t mask = (uint16_t)all;
        /* palancas a tope (con AC3_PAD_ID=73): lizq/lder/larr/laba, rizq/rder/rarr/raba */
        uint8_t ax[4] = {0x80, 0x80, 0x80, 0x80};
        for (int a = 0; a < 4; a++) {
            if (all >> (16 + 2 * a) & 1) ax[a] = 0x00;
            if (all >> (17 + 2 * a) & 1) ax[a] = 0xFF;
        }
        psx_pad_set(0, mask, ax[0], ax[1], ax[2], ax[3]);
        if (getenv("AC3_PADDBG") && mask) {   /* depuracion: lo que ve el juego en su bufer de mando */
            uint32_t pb = *(uint32_t *)psx_ptr(AC3_PAD_BUF);
            const uint8_t *b = psx_ptr(pb);
            printf("fotograma %d: mando %04x, bufer del juego (%08x) %02x %02x %02x %02x\n", last_vcount, mask, pb, b[0], b[1], b[2], b[3]);
        }
        if (shots && every && last_vcount % every == 0) save_screen(last_vcount);
        if (last_vcount >= max_frames) longjmp(stop, 1);
    }
}

/* prueba de la interpolacion (AC3_INTERP=n): cada 100 fotogramas, estadisticas; entre
 * AC3_INTERP_FROM y +8 fotogramas, todas las imagenes (intermedias y reales) a las capturas */
static int interp_from, interp_len = 8;   /* AC3_INTERP_FROM, AC3_INTERP_LEN (VBlank) */
/* prueba con verdad conocida (AC3_INTERP_TEST=S, S impar): PSNR de la intermedia de k y k+S+1
 * frente al paso real del medio, y de los vecinos reales frente a el (lo que se veria sin
 * interpolar). S = 1: k, k+2 frente a k+1; S = 3: movimientos el doble de rapidos */
static uint32_t t_prev[640 * 512], t_mid[640 * 512], t_int[640 * 512];
static int t_have_prev, t_have_mid, t_n, t_fresh;
static double t_sum_int, t_sum_prev, t_sum_next, t_min_gain = 1e9;
static long t_holes_int, t_holes_mid, t_holes_max, t_holes_max_f, t_dark;
/* agujeros (como tools/interp_holes.py): pixeles casi negros en b donde a y c no lo son */
static int holes(const uint32_t *a, const uint32_t *b, const uint32_t *c, int n) {
    int k = 0;
    for (int i = 0; i < n; i++) {
#define MX(v) ((int)((v) & 255) > (int)(((v) >> 8) & 255) ? ((int)((v) & 255) > (int)(((v) >> 16) & 255) ? (int)((v) & 255) : (int)(((v) >> 16) & 255)) : ((int)(((v) >> 8) & 255) > (int)(((v) >> 16) & 255) ? (int)(((v) >> 8) & 255) : (int)(((v) >> 16) & 255)))
        if (MX(b[i]) < 16 && MX(a[i]) >= 40 && MX(c[i]) >= 40) k++;
#undef MX
    }
    return k;
}
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
    if (den == 0) { if (num == (psx_interp_skip_test + 1) / 2) { memcpy(t_mid, rgba, n * 4); t_have_mid = 1; } return; }
    if (num < den) { memcpy(t_int, rgba, n * 4); t_fresh = 1; return; }
    if (t_have_prev && t_have_mid && den == 2 && t_fresh) {
        double pi = psnr(t_int, t_mid, n), pp = psnr(t_prev, t_mid, n), pn = psnr(rgba, t_mid, n);
        t_sum_int += pi; t_sum_prev += pp; t_sum_next += pn; t_n++;
        int hi_ = holes(t_prev, t_int, rgba, n), hm = holes(t_prev, t_mid, rgba, n);
        t_holes_int += hi_; t_holes_mid += hm;
        for (int i = 0; i < n; i++) {   /* oscuro sobre claro: intermedia oscura donde la real del medio es clara */
            int bi = (int)(t_int[i] & 255), gi = (int)((t_int[i] >> 8) & 255), ri = (int)((t_int[i] >> 16) & 255);
            int bm = (int)(t_mid[i] & 255), gm = (int)((t_mid[i] >> 8) & 255), rm = (int)((t_mid[i] >> 16) & 255);
            if (bi < 40 && gi < 40 && ri < 40 && bm >= 70 && gm >= 70 && rm >= 70) t_dark++;
        }
        if (hi_ - hm > t_holes_max) { t_holes_max = hi_ - hm; t_holes_max_f = last_vcount; }
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
    static int seen_step[8], seen_save[8], seen_vb[8], nsteps;
    pthread_mutex_lock(&sink_mu);
    int k = -1;
    for (int i = 0; i < 8; i++) if (seen_t[i] == t_step) k = i;
    if (k < 0) {
        k = nsteps & 7;
        seen_t[k] = t_step;
        seen_step[k] = nsteps++;
        seen_vb[k] = last_vcount;
        seen_save[k] = last_vcount >= interp_from && last_vcount < interp_from + interp_len;
    }
    int step = getenv("AC3_INTERP_NAMEVB") ? seen_vb[k] : seen_step[k], save = seen_save[k];   /* nombre por VBlank (paso 19) */
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

static uint32_t button(const char *b) {
    static const char *names[16] = {"select", "l3", "r3", "start", "arriba", "derecha", "abajo", "izquierda",
                                    "l2", "r2", "l1", "r1", "triangulo", "circulo", "cruz", "cuadrado"};
    for (int i = 0; i < 16; i++) if (!strcmp(names[i], b)) return 1u << i;
    static const char *sticks[8] = {"lizq", "lder", "larr", "laba", "rizq", "rder", "rarr", "raba"};
    for (int i = 0; i < 8; i++) if (!strcmp(sticks[i], b)) return 1u << (16 + i);
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
        uint32_t m = 0;
        for (char *b = strtok_r(btns, "+", &sp2); b; b = strtok_r(NULL, "+", &sp2)) m |= button(b);
        script[nscript].frame = f; script[nscript].mask = m; script[nscript].dur = d;
        nscript++;
    }
}

#ifdef AC3_LIBRE
/* AC3_MISSION=N: al entrar en la funcion que arranca la mision, la mision actual del estado de
 * la partida (g_state + 0x390) pasa a ser N; el juego carga entonces mission_records[N] */
static int g_force_mission = -1;

/* AC3_COVERAGE=archivo: al terminar, las direcciones de las funciones en las que se entro.
 * AC3_STATES=dir + AC3_STATES_LIST=archivo (direcciones en hex) [+ AC3_STATES_MAX=n]: al entrar en
 * una de ellas se guarda el estado (las n primeras veces): magic "AC3S", direccion, 32 registros,
 * hi, lo, GTE (256 B), scratchpad (1 KB), RAM (2 MB). Lo lee tools/recomp/check.py --states */
static const char *g_states_dir;
static int g_states_max = 2;
static uint16_t g_states_n[PSX_RAM_SIZE / 4];
static void save_state(RCpu *c, uint32_t a) {
    uint32_t i = (a & (PSX_RAM_SIZE - 1)) >> 2;
    if (g_states_n[i] >= g_states_max) return;
    char path[600];
    snprintf(path, sizeof path, "%s/st_%08x_%d_f%d.bin", g_states_dir, a, g_states_n[i], last_vcount);
    g_states_n[i]++;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t hdr[2] = {0x53334341u, a};   /* "AC3S" */
    fwrite(hdr, 4, 2, f);
    fwrite(c->r, 4, 32, f);
    fwrite(&c->hi, 4, 1, f);
    fwrite(&c->lo, 4, 1, f);
    fwrite(rc_gte, 1, 256, f);
    fwrite(psx_scratch, 1, 0x400, f);
    fwrite(psx_ram, 1, PSX_RAM_SIZE, f);
    fclose(f);
}
static void coverage_dump(void) {
    const char *p = getenv("AC3_COVERAGE");
    FILE *f = p ? fopen(p, "w") : NULL;
    if (!f) return;
    int n = 0;
    uint32_t ob = AC3_OVL_BASE;
    for (uint32_t a = 0x80000000u; a < ob; a += 4)
        if (mipsi_entered(a)) { fprintf(f, "%08x\n", a); n++; }
    static uint32_t oa[32768], os[32768];
    int no = mipsi_ovl_entries(oa, os, 32768);
    for (int k = 0; k < no; k++) { fprintf(f, "%08x %08x\n", oa[k], os[k]); n++; }
    fclose(f);
    printf("cobertura: %d funciones con entrada -> %s\n", n, p);
}
static void force_mission(RCpu *c) {
    (void)c;
    uint32_t st = *(uint32_t *)psx_ptr(V->g_state_ptr);
    int16_t *m = (int16_t *)psx_ptr(st + 0x390);
    if (g_force_mission < 0) {   /* solo la primera vez: las siguientes las encadena el juego */
        printf("mision: el juego arranca la %d en el fotograma %d\n", *m, last_vcount);
        fflush(stdout);
        return;
    }
    printf("mision: el juego arranca la %d en el fotograma %d; se fuerza la %d\n", *m, last_vcount, g_force_mission);
    fflush(stdout);
    *m = (int16_t)g_force_mission;
    g_force_mission = -1;
}

/* AC3_TRACE=dir[,dir...] (hex): cada entrada a esas funciones, con el VBlank, quien llama (ra) y
 * los argumentos a0-a3 (etapa 8: localizar el dibujo de la mision). AC3_TRACE_FROM: desde ese
 * VBlank; hasta 400 lineas */
static void trace_hook(RCpu *c) {
    static int n;
    static int from = -1;
    if (from < 0) from = getenv("AC3_TRACE_FROM") ? atoi(getenv("AC3_TRACE_FROM")) : 0;
    if (last_vcount < from || n >= 400) return;
    n++;
    printf("traza: VBlank %d pc %08x ra %08x a0 %08x a1 %08x a2 %08x a3 %08x\n", last_vcount, c->r[0] == 0 ? 0u : 0u, c->r[31], c->r[4],
           c->r[5], c->r[6], c->r[7]);
}

/* ---- etapa 8 (psx/recomp/ac3_rerender.c): AC3_RR_SELFTEST=vblank comprueba que con t = 1 la lista
 * redibujada da la misma imagen que la original; AC3_RR_INTERP=desde,cuantos guarda en cada paso la
 * imagen real (i<VBlank>_2-2.ppm) y la intermedia a medio camino (i<VBlank>_1-2.ppm) ---- */
#include "ac3_rerender.h"
#include "ac3_rr_async.h"
#include "psx_gpu.h"
extern void (*psx_draw_list_hook)(PsxGpu *g, uint32_t ot);
/* AC3_UNCOVERED=dir: pixeles de la imagen que no dibuja ninguna primitiva (el area se rellena antes
 * con un color testigo); en la mision, el cielo y el terreno deberian cubrirlo todo. Guarda las
 * imagenes con pixeles sin cubrir (u<VBlank>.ppm, el testigo en magenta) */
static const char *g_uncov_dir;
static int g_uncov_frames, g_uncov_bad;
static void uncov_summary(void) { printf("descubiertos: %d imagenes, %d con pixeles sin dibujar\n", g_uncov_frames, g_uncov_bad); }
static void uncov_hook(PsxGpu *g, uint32_t ot) {
    int x1 = g->area_x1, y1 = g->area_y1, x2 = g->area_x2, y2 = g->area_y2;
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++) g->vram[y * 1024 + x] = 0x7C1F;
    psx_gpu_draw_list(g, ot);
    int n = 0, nl = 0, nr = 0, w = x2 - x1 + 1;
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            if (g->vram[y * 1024 + x] == 0x7C1F) { n++; if (x - x1 < w / 8) nl++; else if (x2 - x >= 0 && x2 - x < w / 8) nr++; }
    if (ac3_terrain_count == 0) return;   /* solo en la mision */
    g_uncov_frames++;
    if (!n) return;
    g_uncov_bad++;
    if (g_uncov_bad <= 40) {
        printf("descubiertos: VBlank %d: %d pixeles (borde izquierdo %d, derecho %d)\n", last_vcount, n, nl, nr);
        char fn[512];
        snprintf(fn, sizeof fn, "%s/u%05d.ppm", g_uncov_dir, last_vcount);
        FILE *f = fopen(fn, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, y2 - y1 + 1);
            for (int y = y1; y <= y2; y++)
                for (int x = x1; x <= x2; x++) {
                    uint16_t p = g->vram[y * 1024 + x];
                    uint8_t c[3] = {(uint8_t)((p & 31) << 3), (uint8_t)((p >> 5 & 31) << 3), (uint8_t)((p >> 10 & 31) << 3)};
                    fwrite(c, 1, 3, f);
                }
            fclose(f);
        }
    }
}
/* AC3_SKYCHECK=1: las franjas del cielo (poligonos sin textura antes de la primera orden E3 de la
 * lista, paso 19) se recortan al rectangulo de la pantalla, asi que todos sus vertices deberian
 * estar en el borde (|x| = 160 o |y| = 120 en la US; 128 en la EU). Un vertice dentro es una cuna
 * falsa (pantalla ancha con el avion ladeado, etapa 8). Con AC3_SKYDUMP=vb1,vb2 ademas vuelca los
 * paquetes del cielo de esas imagenes */
static int g_sky0 = -1, g_sky1 = -1, g_sky_frames, g_sky_bad, g_sky_badv;
static void sky_summary(void) { printf("cielo: %d imagenes, %d con vertices de franja dentro de la pantalla (%d vertices)\n", g_sky_frames, g_sky_bad, g_sky_badv); }
static void sky_hook(PsxGpu *g, uint32_t ot) {
    int dump = last_vcount >= g_sky0 && last_vcount <= g_sky1, bad = 0, any = 0;
    int hx = (g->area_x2 - g->area_x1 + 1) / 2, hy = (g->area_y2 - g->area_y1 + 1) / 2;
    if (dump) printf("cielo vb %d:\n", last_vcount);
    uint32_t a = ot;
    for (int n = 0; (a & 0xFFFFFF) != 0xFFFFFF && n < 100000; n++) {
        uint32_t *p = (uint32_t *)psx_ptr(a | 0x80000000u), len = p[0] >> 24;
        if (len) {
            uint32_t op = p[1] >> 24;
            if (op == 0xE3) break;
            if ((op & 0xE4) == 0x20) {   /* poligono sin textura: plano (1 color) o degradado */
                int nv = (op & 8) ? 4 : 3, step = (op & 0x10) ? 2 : 1;
                any = 1;
                if (dump) printf("  op %02x:", op);
                for (int v = 0; v < nv; v++) {
                    uint32_t w = p[2 + v * step];
                    int x = (int16_t)(w << 5) >> 5, y = (int16_t)((w >> 16) << 5) >> 5;
                    if (dump) printf(" (%d,%d)", x, y);
                    if (abs(x) < hx - 1 && abs(y) < hy - 1) bad++;
                }
                if (dump) printf("\n");
            }
        }
        a = p[0] & 0xFFFFFF;
    }
    if (any && ac3_terrain_count) {
        g_sky_frames++;
        if (bad && ++g_sky_bad <= 5) printf("cielo: cuna en el VBlank %d (%d vertices)\n", last_vcount, bad);
        g_sky_badv += bad;
    }
    psx_gpu_draw_list(g, ot);
}
static int rr_self = -1, rr_from = -1, rr_len, rr_every, rr_tests, rr_bad, rr_skipped;
static void rr_self_summary(void) {
    printf("rr: autoprueba t = 1: %d imagenes comprobadas, %d distintas del original, %d sin construir (corte o pausa)\n", rr_tests, rr_bad,
           rr_skipped);
}
static int g_rr_async;
static void rr_stats(void) {
    extern double psx_interp_rebuild_s;
    extern int psx_interp_rebuild_n, psx_interp_rebuild_fails;
    printf("rr: %d pasos redibujados %s (%d sin poder: corte de plano o sin datos), %.2f ms de media por paso\n", psx_interp_rebuild_n,
           g_rr_async ? "en un hilo de dibujo" : "en el hilo del juego", psx_interp_rebuild_fails,
           psx_interp_rebuild_n ? 1000.0 * psx_interp_rebuild_s / psx_interp_rebuild_n : 0.0);
    PsxInterpPerf pf;
    psx_interp_perf(&pf, 0);
    printf("rr: pasos %d, con intermedias %d, saltados (hilos ocupados) %d; hilo de dibujo %.2f ms de media, %.2f el peor\n", pf.steps, pf.jobs,
           pf.skipped, pf.worker_ms, pf.worker_max_ms);
    extern double ac3_rr_t[6];
    double t[6];
    if (g_rr_async) ac3_rr_async_times(t);
    else memcpy(t, ac3_rr_t, sizeof t);
    double n = t[5] > 0 ? t[5] : 1;
    printf("rr: ms por redibujo: cadena %.2f, restaurar %.2f, dibujo del juego %.2f, cajones %.2f; S0 (hilo del juego, cada paso) %.2f\n",
           1000 * t[1] / n, 1000 * t[2] / n, 1000 * t[3] / n, 1000 * t[4] / n, 1000 * ac3_rr_t[0] / (ac3_rr_t[5] > 0 && !g_rr_async ? ac3_rr_t[5] : n));
}
static void save_vram(const PsxGpu *g, const char *name) {
    int w = g->area_x2 - g->area_x1 + 1, h = g->area_y2 - g->area_y1 + 1;
    char path[600];
    snprintf(path, sizeof path, "%s/%s", shots, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t c = g->vram[(g->area_y1 + y) * 1024 + g->area_x1 + x];
            uint8_t px[3] = {(uint8_t)((c & 31) << 3 | (c & 31) >> 2), (uint8_t)(((c >> 5) & 31) << 3 | ((c >> 5) & 31) >> 2),
                             (uint8_t)(((c >> 10) & 31) << 3 | ((c >> 10) & 31) >> 2)};
            fwrite(px, 1, 3, f);
        }
    fclose(f);
}
/* construye la intermedia de fase t en una copia de la GPU (gout) sin tocar la partida */
static int rr_draw_phase(PsxGpu *gout, uint32_t ot, float t) {
    static uint8_t ram[PSX_RAM_SIZE], scr[0x400];
    static PsxGte gte;
    PsxGte *gc = (PsxGte *)psx_gte_get_context();
    memcpy(ram, psx_ram, sizeof ram); memcpy(scr, psx_scratch, sizeof scr); gte = *gc;
    int r = ac3_rr_build(ot, t);
    if (r == 0) psx_gpu_draw_list(gout, ot);
    memcpy(psx_ram, ram, sizeof ram); memcpy(psx_scratch, scr, sizeof scr); *gc = gte;
    return r;
}
static void rr_hook(PsxGpu *g, uint32_t ot) {
    if (rr_self >= 0 && last_vcount >= rr_self && ac3_rr_ready()) {   /* t = 1: igual que el original */
        static PsxGpu a, b;
        static PsxHwPrim p0[65536], p1[65536];
        static uint16_t d0[1024 * 512], d1[1024 * 512];
        PsxHwList l0 = {.p = p0, .max = 65536, .data = d0, .maxdata = 1024 * 512, .main_x1 = g->area_x1, .main_y1 = g->area_y1,
                        .main_x2 = g->area_x2, .main_y2 = g->area_y2};
        PsxHwList l1 = l0;
        l1.p = p1; l1.data = d1;
        a = *g; b = *g;
        psx_gpu_record(&l0, 0);
        psx_gpu_draw_list(&a, ot);
        psx_gpu_record_flush(&a);
        psx_gpu_record(&l1, 0);
        int r = rr_draw_phase(&b, ot, 1.0f);
        psx_gpu_record_flush(&b);
        psx_gpu_record(NULL, 0);
        printf("rr: primitivas %d / %d\n", l0.n, l1.n);
        for (int i = 0, shown = 0; i < l0.n && i < l1.n && shown < 6; i++) {
            PsxHwPrim x = p0[i], y = p1[i];
            x.data_off = y.data_off = 0;
            if (!memcmp(&x, &y, sizeof x)) continue;
            shown++;
            printf("  primitiva %d: tipo %d/%d banderas %x/%x xy0 (%d,%d)/(%d,%d) color %d,%d,%d/%d,%d,%d\n", i, x.kind, y.kind, x.flags, y.flags,
                   x.x[0], x.y[0], y.x[0], y.y[0], x.r[0], x.g[0], x.b[0], y.r[0], y.g[0], y.b[0]);
        }
        int nd = 0;
        for (int y = g->area_y1; y <= g->area_y2; y++)
            for (int x = g->area_x1; x <= g->area_x2; x++) nd += a.vram[y * 1024 + x] != b.vram[y * 1024 + x];
        if (rr_every) {   /* sistematica: resumen al salir y las primeras que fallen */
            rr_tests++;
            if (r) rr_skipped++;
            else if (nd) { rr_bad++; if (rr_bad <= 5) printf("rr: prueba t = 1 en el VBlank %d: %d pixeles distintos\n", last_vcount, nd); }
            rr_self = last_vcount + rr_every;
        } else {
            printf("rr: prueba t = 1 en el VBlank %d: %s, %d pixeles distintos del original\n", last_vcount, r ? "sin construir" : "construida", nd);
            rr_self = -1;
        }
    }
    if (rr_from >= 0 && last_vcount >= rr_from && last_vcount < rr_from + rr_len && ac3_rr_ready() && shots) {
        static PsxGpu mid;
        mid = *g;
        char name[64];
        if (rr_draw_phase(&mid, ot, 0.5f) == 0) {
            snprintf(name, sizeof name, "i%05d_1-2.ppm", last_vcount);
            save_vram(&mid, name);
        }
        psx_gpu_draw_list(g, ot);
        snprintf(name, sizeof name, "i%05d_2-2.ppm", last_vcount);
        save_vram(g, name);
        return;
    }
    psx_gpu_draw_list(g, ot);
}

/* AC3_SEQ=v: al entrar en el primer paso del modo 5 (la secuencia que reproduce el juego segun
 * g_state + 0x3AC: 0 la demostracion del menu, 2 el final de la campana, que sigue en los
 * creditos) se escribe v en g_state + 0x3AC, la primera vez */
static int g_force_seq = -1;
static void force_seq(RCpu *c) {
    (void)c;
    uint32_t st = *(uint32_t *)psx_ptr(V->g_state_ptr);
    uint8_t *q = (uint8_t *)psx_ptr(st + 0x3AC);
    printf("secuencia: el juego entra en el modo 5 con 0x3AC = %d en el fotograma %d%s\n", *q, last_vcount,
           g_force_seq >= 0 ? "; se fuerza" : "");
    if (g_force_seq >= 0) { printf("  -> %d\n", g_force_seq); *q = (uint8_t)g_force_seq; g_force_seq = -1; }
    fflush(stdout);
}
#endif

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "uso: %s <imagen> <ace_eu.bin> <fotogramas> [capturas] [cada_n] [guion]\n", argv[0]); return 2; }
    max_frames = atoi(argv[3]);
    shots = argc > 4 ? argv[4] : NULL;
    every = argc > 5 ? atoi(argv[5]) : 100;
    if (argc > 6) parse_script(argv[6]);
    /* ACE.BIN en la RAM (0x80010000) y tablas de la GTE */
    static uint8_t ace[0x200000];
#ifdef AC3_LIBRE
    (void)argv[2];   /* modo libre: ACE.BIN sale del disco */
    if (psx_cd_open(argv[1])) { puts("no se abre la imagen"); return 1; }
    V = ac3_version_detect();
    if (!V) { puts("el disco no es de ninguna version conocida"); return 1; }
    PsxAceBin ab;
    int ar = psx_acebin_from_disc(ace, sizeof ace, &ab);
    if (ar) { printf("ACE.BIN del disco: fallo %d\n", ar); return 1; }
    size_t n = ab.size;
    memcpy(psx_ram + (ab.load & 0x1FFFFFu), ace, n);
    psx_gte_tables_init(ace, n);
    printf("modo libre: version %s, ACE.BIN del disco (%zu bytes)\n", V->name, n);
#else
    FILE *f = fopen(argv[2], "rb");
    if (!f) { puts("no se abre ACE.BIN"); return 1; }
    size_t n = fread(ace, 1, sizeof ace, f);
    fclose(f);
    memcpy(psx_ram + 0x10000, ace, n);
    psx_gte_tables_init(ace, n);
    if (psx_cd_open(argv[1])) { puts("no se abre la imagen"); return 1; }
#endif
    psx_pad_connect(0, getenv("AC3_PAD_ID") ? (int)strtol(getenv("AC3_PAD_ID"), NULL, 16) : 0x41);   /* 73 = analogico */
    psx_pad_connect(1, 0);
    psx_card_attach(0, getenv("AC3_CARD") ? getenv("AC3_CARD") : "/tmp/ac3_native_tarjeta1.mcd");   /* AC3_CARD: otra tarjeta */
    psx_card_detach(1);
    psx_set_refresh(AC3_REFRESH);
    /* codigo recompilado: residente, overlays (deteccion por firma) y enlace con psx/ */
    rc_set_region(AC3_OVL_BASE, AC3_LIB_LO, AC3_LIB_HI, AC3_LIB_MAIN, AC3_GP);
#ifdef AC3_LIBRE
    rc_set_interpreter(mipsi_call);
    rc_set_hle(V->hle, *V->nhle);
    if (getenv("AC3_COVERAGE")) { mipsi_coverage(1); atexit(coverage_dump); }
    if (getenv("AC3_STATES") && getenv("AC3_STATES_LIST")) {
        static uint32_t list[8192];
        int n = 0;
        FILE *lf = fopen(getenv("AC3_STATES_LIST"), "r");
        while (lf && n < 8192 && fscanf(lf, "%x", &list[n]) == 1) n++;
        if (lf) fclose(lf);
        g_states_dir = getenv("AC3_STATES");
        if (getenv("AC3_STATES_MAX")) g_states_max = atoi(getenv("AC3_STATES_MAX"));
        mipsi_watch(list, n, save_state);
        printf("estados: %d funciones vigiladas -> %s\n", n, g_states_dir);
    }
    if (getenv("AC3_SEQ")) {   /* prueba: forzar la secuencia del modo 5 (final: 2) */
        g_force_seq = atoi(getenv("AC3_SEQ"));
        mipsi_set_hook(V->seq_start, force_seq);
    }
#ifdef AC3_GTEREF
    if (getenv("AC3_GTEREF")) {   /* paso 18: cada orden de la GTE, tambien en la de referencia */
        void psx_gteref_start(void);
        psx_gteref_start();
        puts("GTE de referencia: activa");
    }
#endif
    if (getenv("AC3_HWCMP")) {   /* paso 17: comparar el dibujo por GLES con el software, lista a lista */
        int psx_hwcmp_start(const char *dir);
        void psx_hwcmp_interp(void);
        if (psx_hwcmp_start(getenv("AC3_HWCMP"))) puts("AC3_HWCMP: sin contexto GLES");
        else if (getenv("AC3_INTERP")) psx_hwcmp_interp();   /* tambien las intermedias */
    }
    if (getenv("AC3_WIDE")) {   /* pantalla ancha en la mision */
        g_wide = 1;
        ac3_wide_install(V);
    }
    if (getenv("AC3_RR")) {   /* etapa 8: intermedias con el mundo estatico redibujado (con AC3_INTERP y AC3_INTERP_THREADS) */
        /* 1: en un hilo de dibujo (la segunda copia del interprete); 2: en el hilo del juego */
        g_rr_async = atoi(getenv("AC3_RR")) != 2;
        if (ac3_rr_install(V) || (g_rr_async && ac3_rr_async_install(V))) puts("AC3_RR: esta version aun no tiene direcciones");
        else {
            if (g_rr_async) psx_interp_rebuild_async(ac3_rr_async());
            else psx_interp_rebuild(ac3_rr_build);
            atexit(rr_stats);
        }
    }
    if (getenv("AC3_RR_SELFTEST") || getenv("AC3_RR_INTERP") || getenv("AC3_RR_SURVEY")) {   /* etapa 8 */
        if (ac3_rr_install(V)) puts("AC3_RR: esta version aun no tiene direcciones");
        if (getenv("AC3_RR_SELFTEST")) rr_self = atoi(getenv("AC3_RR_SELFTEST"));
        if (getenv("AC3_RR_SELFTEST_EVERY")) { rr_every = atoi(getenv("AC3_RR_SELFTEST_EVERY")); atexit(rr_self_summary); }
        if (getenv("AC3_RR_INTERP")) sscanf(getenv("AC3_RR_INTERP"), "%d,%d", &rr_from, &rr_len);
        psx_draw_list_hook = rr_hook;
    }
    if (getenv("AC3_WATCHWRITE")) { extern uint32_t mipsi_watch_write; mipsi_watch_write = (uint32_t)strtoul(getenv("AC3_WATCHWRITE"), NULL, 16); }
    if (getenv("AC3_SKYCHECK") || getenv("AC3_SKYDUMP")) {
        if (getenv("AC3_SKYDUMP")) sscanf(getenv("AC3_SKYDUMP"), "%d,%d", &g_sky0, &g_sky1);
        psx_draw_list_hook = sky_hook; atexit(sky_summary);
        if (!g_wide) ac3_wide_install(V);   /* para ac3_terrain_count (psx_gte_wide sigue en 1) */
    }
    if (getenv("AC3_UNCOVERED")) {
        g_uncov_dir = getenv("AC3_UNCOVERED"); psx_draw_list_hook = uncov_hook; atexit(uncov_summary);
        if (!g_wide) ac3_wide_install(V);   /* solo para saber cuando se dibuja terreno (psx_gte_wide sigue en 1) */
    }
    if (getenv("AC3_TRACE")) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s", getenv("AC3_TRACE"));
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) mipsi_set_hook((uint32_t)strtoul(t, NULL, 16), trace_hook);
    }
    if (getenv("AC3_MISSION")) {   /* prueba: forzar la mision que carga el juego */
        g_force_mission = atoi(getenv("AC3_MISSION"));
        mipsi_set_hook(V->mission_start, force_mission);
    }
#else
    if (getenv("AC3_MIPSI")) {
        /* etapa 7: sin codigo traducido, todo lo ejecuta el interprete (psx/recomp/mipsi.c) */
        rc_set_interpreter(mipsi_call);
        puts("interprete MIPS: sin codigo traducido");
    } else {
        rc_register(AC3_RC_FUNCS, AC3_RC_NFUNCS);
        rc_register_overlay(1, rc_funcs_overlay1, rc_nfuncs_overlay1);
        rc_register_overlay(2, rc_funcs_overlay2, rc_nfuncs_overlay2);
        rc_register_overlay(3, rc_funcs_overlay3, rc_nfuncs_overlay3);
        rc_register_overlay(4, rc_funcs_overlay4, rc_nfuncs_overlay4);
        rc_register_overlay(5, rc_funcs_overlay5, rc_nfuncs_overlay5);
        rc_set_overlay_sigs(ovl_sig, 5);
    }
    rc_set_hle(hle_table, hle_count);
#endif
    rc_gte = psx_gte_get_context();
    psx_set_call(rc_host_callback);
    rc_set_tick(tick, BUDGET_PER_FRAME);
    rc_set_frame_hook(on_vsync_frame);
    if (getenv("AC3_INTERP")) {
        interp_from = getenv("AC3_INTERP_FROM") ? atoi(getenv("AC3_INTERP_FROM")) : 0;
        if (getenv("AC3_INTERP_LEN")) interp_len = atoi(getenv("AC3_INTERP_LEN"));
        psx_interp_skip_test = getenv("AC3_INTERP_TEST") ? atoi(getenv("AC3_INTERP_TEST")) : 0;
        psx_interp_config(atoi(getenv("AC3_INTERP")), psx_interp_skip_test ? 1 : 2, interp_sink);
        if (getenv("AC3_INTERP_THREADS")) psx_interp_threads(atoi(getenv("AC3_INTERP_THREADS")), NULL);
    }
    /* como ac3_start: BSS a cero y ac3_main */
    memset(psx_ram + (AC3_BSS_START & 0x1FFFFFu), 0, AC3_BSS_END - AC3_BSS_START);
    RCpu c;
    memset(&c, 0, sizeof c);
    c.r[28] = AC3_GP;
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
    if (t_n) printf("oscuro sobre claro por imagen: %.1f\n", (double)t_dark / t_n);
    if (t_n) printf("agujeros negros por imagen: interpolada %.1f, real del medio %.1f (ruido de la medida); peor exceso %ld (fotograma %ld)\n",
                    (double)t_holes_int / t_n, (double)t_holes_mid / t_n, t_holes_max, t_holes_max_f);
    if (mipsi_steps) printf("interprete: %llu instrucciones (%.1f millones por segundo de CPU)\n", (unsigned long long)mipsi_steps,
                            mipsi_steps / 1e6 / (dt > 0 ? dt : 1));
    printf("%d fotogramas en %.2f s de CPU (%.0f fotogramas/s; tiempo real del juego: %.1f s a %d Hz)\n", last_vcount, dt,
           last_vcount / (dt > 0 ? dt : 1), last_vcount / (double)AC3_REFRESH, AC3_REFRESH);
    return 0;
}
