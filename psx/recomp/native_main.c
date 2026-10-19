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

static void frame_check(void) {
    int v = VSync(-1);
    while (last_vcount < v) {
        last_vcount++;
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

/* ---- etapa 8, prueba de concepto (AC3_RERENDER=vblank, direcciones de la EU): en ese VBlank, al
 * llegar a DrawOTag, se quita de la tabla de orden lo que dibujo el mundo estatico (terreno, cielo,
 * objetos de escena: lo que FUN_80033748 dibuja despues de la logica), se gira la camara y se
 * vuelven a llamar las funciones de dibujo del juego; se dibuja esa imagen y se restaura todo. Si
 * el terreno y el cielo salen girados y el HUD y el avion igual, el dibujo del juego se puede
 * repetir con otra camara ---- */
#include "psx_gpu.h"
#include "psx_libgte.h"
#define RR_G_RENDER 0x800BACC4u   /* puntero a la estructura de render */
#define RR_G_STATE 0x800BACD0u
static uint32_t rr_st0, rr_st1;   /* primitivas del mundo estatico: [rr_st0, rr_st1) */
static int rr_vblank = -1;
static uint32_t rd32(uint32_t a) { return *(uint32_t *)psx_ptr(a); }
static void wr32(uint32_t a, uint32_t v) { *(uint32_t *)psx_ptr(a) = v; }
static int rr_busy;   /* durante el redibujo no se mueven las marcas */
static void rr_mark0(RCpu *c) { (void)c; if (!rr_busy) rr_st0 = rd32(rd32(RR_G_RENDER) + 20); }            /* FUN_8003308c */
static void rr_mark1(RCpu *c) { (void)c; if (!rr_busy) rr_st1 = rd32(rd32(RR_G_RENDER) + 20) - 12; }       /* FUN_80039d84 */
extern uint8_t *mipsi_rtrace, *mipsi_wtrace;
static uint8_t rr_prev[PSX_RAM_SIZE];   /* RAM del paso anterior (en su DrawOTag) */
static int rr_have_prev;
/* encuesta (AC3_RERENDER_SURVEY=pasos): por palabra leida, cuantas veces cambio y el mayor salto */
typedef struct { uint32_t a; int n; int64_t maxd; uint32_t last; } RrStat;
static RrStat rr_st[4096];
static int rr_nst, rr_steps;
static void rr_survey_add(uint32_t a, uint32_t was, uint32_t now) {
    int i = 0;
    while (i < rr_nst && rr_st[i].a != a) i++;
    if (i == rr_nst) { if (rr_nst == 4096) return; rr_st[rr_nst].a = a; rr_st[rr_nst].n = 0; rr_st[rr_nst].maxd = 0; rr_nst++; }
    int64_t d = (int64_t)(int32_t)now - (int32_t)was;
    if (d < 0) d = -d;
    rr_st[i].n++;
    if (d > rr_st[i].maxd) rr_st[i].maxd = d;
    rr_st[i].last = now;
}
static void rr_survey_print(void) {
    uint32_t gr = rd32(RR_G_RENDER), cam = rd32(gr + 40);
    printf("encuesta: %d pasos; g_render %08x, camara %08x\n", rr_steps, gr, cam);
    for (int i = 0; i < rr_nst; i++) {   /* por direccion */
        for (int j = i + 1; j < rr_nst; j++) if (rr_st[j].a < rr_st[i].a) { RrStat t = rr_st[i]; rr_st[i] = rr_st[j]; rr_st[j] = t; }
        uint32_t A = 0x80000000u | rr_st[i].a;
        char where[48] = "";
        if (A >= cam && A < cam + 0x200) snprintf(where, sizeof where, "camara+%03x", A - cam);
        else if (A >= gr && A < gr + 0x1000) snprintf(where, sizeof where, "g_render+%03x", A - gr);
        printf("  %08x %-14s cambia %3d de %d, salto max %lld, ultimo %08x\n", A, where, rr_st[i].n, rr_steps, (long long)rr_st[i].maxd, rr_st[i].last);
    }
}
/* llamada a una funcion del juego con hasta 4 argumentos, como una interrupcion (pila propia) */
static void guest_call(uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    RCpu cb;
    memset(&cb, 0, sizeof cb);
    cb.r[4] = a0; cb.r[5] = a1; cb.r[6] = a2; cb.r[7] = a3;
    cb.r[28] = V->gp;
    cb.r[29] = 0x8000E000u;
    cb.r[31] = 0x8000FFF0u;
    uint32_t ret = rc_ret;
    rc_ret = cb.r[31];
    rc_call(&cb, addr);
    rc_ret = ret;
}
extern void (*psx_draw_list_hook)(PsxGpu *g, uint32_t ot);
static void rr_map(uint32_t ot, const char *when) {   /* depuracion: paquetes de la cadena por zona de memoria y orden de GPU */
    {
        uint32_t gr0 = rd32(RR_G_RENDER);
        printf("rerender (%s): ot %08x, g_render[4] %08x [5] %08x, bufers de primitivas %08x %08x, ots %08x %08x\n", when, ot, rd32(gr0 + 16), rd32(gr0 + 20),
               rd32(gr0 + 32), rd32(gr0 + 36), rd32(gr0 + 24), rd32(gr0 + 28));
        int hist[4][256];
        memset(hist, 0, sizeof hist);
        uint32_t b = ot, gd = 0;
        while ((b & 0xFFFFFF) != 0xFFFFFF && ++gd < 300000) {
            uint32_t tag = rd32(b | 0x80000000u), n = tag >> 24;
            int zone = (b | 0x80000000u) < rr_st0 ? 0 : (b | 0x80000000u) < rr_st1 ? 1 : 2;
            if (((b | 0x80000000u) & 0xFFE00000u) == 0x1F800000u) zone = 3;
            if (n) hist[zone][rd32((b | 0x80000000u) + 4) >> 24]++;
            b = tag & 0xFFFFFF;
        }
        static const char *ZN[4] = {"antes", "rango", "despues", "scratch"};
        for (int z = 0; z < 4; z++) {
            printf("  %s:", ZN[z]);
            for (int c2 = 0; c2 < 256; c2++) if (hist[z][c2]) printf(" %02x:%d", c2, hist[z][c2]);
            printf("\n");
        }
    }
}
static void rr_hook(PsxGpu *g, uint32_t ot) {
    if (last_vcount < rr_vblank || rr_vblank < 0 || !rr_st0 || rr_st1 <= rr_st0) {
        if (rr_vblank >= 0 && last_vcount >= rr_vblank - 8) { memcpy(rr_prev, psx_ram, sizeof rr_prev); rr_have_prev = 1; }
        psx_gpu_draw_list(g, ot);
        return;
    }
    static uint8_t ram[PSX_RAM_SIZE], scr[0x400];
    static PsxGte gte;
    memcpy(ram, psx_ram, sizeof ram); memcpy(scr, psx_scratch, sizeof scr);
    PsxGte *gc = (PsxGte *)psx_gte_get_context();
    gte = *gc;
    if (getenv("AC3_RERENDER_MAP")) rr_map(ot, "antes");
    /* 1. quitar de la cadena los paquetes del mundo estatico */
    uint32_t a = ot, removed = 0, guard = 0;
    while ((a & 0xFFFFFF) != 0xFFFFFF && ++guard < 300000) {
        uint32_t tag = rd32(a | 0x80000000u), next = tag & 0xFFFFFF;
        while (next != 0xFFFFFF && (next | 0x80000000u) >= rr_st0 && (next | 0x80000000u) < rr_st1) {
            next = rd32(next | 0x80000000u) & 0xFFFFFF;
            removed++;
        }
        wr32(a | 0x80000000u, (tag & 0xFF000000u) | next);
        a = next | 0x80000000u;
    }
    /* 2. camara girada y el dibujo estatico otra vez, en el otro bufer de primitivas */
    uint32_t gr = rd32(RR_G_RENDER), gs = rd32(RR_G_STATE), cam = rd32(gr + 40);
    int cur = (int)rd32(gs + 4);
    (void)cur;   /* las primitivas nuevas, tras las de esta imagen (g_render[5] ya apunta ahi; el otro bufer
                  * tiene primitivas 2D de esta misma imagen) */
    int16_t *ang = (int16_t *)psx_ptr(cam + 0x14);
    printf("rerender: VBlank %d, %u paquetes estaticos quitados [%08x, %08x); camara %08x angulos %d %d %d\n", last_vcount, removed, rr_st0,
           rr_st1, cam, ang[0], ang[1], ang[2]);
    ang[1] += getenv("AC3_RERENDER_YAW") ? atoi(getenv("AC3_RERENDER_YAW")) : 64;
    if (getenv("AC3_RERENDER_NODRAW")) goto draw;   /* prueba: solo quitar */
    static uint8_t rt[PSX_RAM_SIZE / 32], wt[PSX_RAM_SIZE / 32];
    memset(rt, 0, sizeof rt);
    memset(wt, 0, sizeof wt);
    if (getenv("AC3_RERENDER_READS")) { mipsi_rtrace = rt; mipsi_wtrace = wt; }
    rr_busy = 1;
    guest_call(0x8003304Cu, cam, gr + 0x2C, gr + 0x4C, 0);
    guest_call(0x8003308Cu, 0, 0, 0, 0);
    guest_call(0x80033140u, 0, 0, 0, 0);
    guest_call(0x80033194u, 0, 0, 0, 0);
    guest_call(0x800331D4u, 0, 0, 0, 0);
    rr_busy = 0;
    mipsi_rtrace = mipsi_wtrace = NULL;
    if (getenv("AC3_RERENDER_READS") && rr_have_prev) {
        /* lo leido que cambio desde el paso anterior (sin las primitivas ni las tablas de orden, que
         * se reescriben cada imagen): lo que habria que interpolar */
        uint32_t ex0 = rd32(gr + 24) & 0x1FFFFF, ex1 = (rd32(gr + 36) & 0x1FFFFF) + 0x20000;
        int nread = 0, nchg = 0;
        for (uint32_t a = 0; a < PSX_RAM_SIZE; a += 4) {
            if (!(rt[a >> 5] >> ((a >> 2) & 7) & 1)) continue;
            nread++;
            if (a >= ex0 && a < ex1) continue;
            uint32_t now = *(uint32_t *)(ram + a), was = *(uint32_t *)(rr_prev + a);
            if (now == was) continue;
            nchg++;
            rr_survey_add(a, was, now);
            if (nchg <= 120 && !getenv("AC3_RERENDER_SURVEY")) printf("  leido y cambiado: %08x  antes %08x  ahora %08x\n", 0x80000000u | a, was, now);
        }
        rr_steps++;
        if (!getenv("AC3_RERENDER_SURVEY"))
            printf("rerender: %d palabras leidas, %d cambiadas desde el paso anterior (sin primitivas ni OT %06x-%06x)\n", nread, nchg, ex0, ex1);
    }
draw:
    if (getenv("AC3_RERENDER_MAP")) rr_map(ot, "despues");
    psx_gpu_draw_list(g, ot);
    /* 3. todo como estaba */
    memcpy(psx_ram, ram, sizeof ram); memcpy(psx_scratch, scr, sizeof scr);
    *gc = gte;
    static int survey_left = -1;
    if (survey_left < 0) survey_left = getenv("AC3_RERENDER_SURVEY") ? atoi(getenv("AC3_RERENDER_SURVEY")) : 0;
    memcpy(rr_prev, ram, sizeof rr_prev);   /* (la RAM de este paso, para el siguiente) */
    rr_have_prev = 1;
    if (survey_left-- > 0) return;   /* encuesta: seguir en el siguiente paso */
    rr_vblank = -1;   /* una vez */
    if (getenv("AC3_RERENDER_SURVEY")) rr_survey_print();
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
    if (getenv("AC3_RERENDER")) {
        rr_vblank = atoi(getenv("AC3_RERENDER"));
        mipsi_set_hook(0x8003308Cu, rr_mark0);
        mipsi_set_hook(0x80039D84u, rr_mark1);
        psx_draw_list_hook = rr_hook;
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
