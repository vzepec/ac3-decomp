/* Prueba de escritorio de la demo de la capa PsyQ (src/psxdemo.c).
 *   cc -O2 -Isrc -I../psx/include test/test_psxdemo.c src/ace.c src/music.c src/psxdemo.c ../psx/src/(todos) -lm -lpthread -o build/test_psxdemo
 *   build/test_psxdemo <imagen.bin> <dir_salida>
 * Pelicula de introduccion: se consume el audio a ritmo real (music_mix cada 20 ms, como
 * el mezclador) durante 12 s; se cuentan los fotogramas distintos mostrados y se comprueba
 * que el audio no se queda sin datos. Terreno de la mision 6 desde su inicio. Escribe
 * imagenes PPM de muestra. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ace.h"
#include "music.h"
#include "psxdemo.h"

static void save(const char *dir, const char *name, const uint32_t *img, int w, int h) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(&img[i], 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "uso: %s <imagen> <dir>\n", argv[0]); return 2; }
    Disc d;
    AcePack p;
    if (disc_open(&d, argv[1]) || pack_open(&d, &p)) { puts("disco"); return 1; }
    int fails = 0;
    if (psxdemo_start(&d, &p, PSXDEMO_INTRO, 6, 0, 0, 0)) { puts("no arranca la pelicula"); return 1; }
    static float acc[2 * 882];
    const uint32_t *last = NULL;
    int distinct = 0, starved = 0, w = 0, h = 0;
    uint32_t first_sig = 0, sig_prev = 0;
    for (int step = 0; step < 600; step++) {   /* 600 x 20 ms = 12 s */
        struct timespec ts = {0, 20 * 1000000L};
        nanosleep(&ts, NULL);
        uint32_t before = music_played();
        memset(acc, 0, sizeof acc);
        music_mix(acc, 882, 1.0f);
        if (step > 25 && music_played() - before < 882) starved++;
        const uint32_t *img = psxdemo_frame(&w, &h, 0.02f);
        if (img) {
            uint32_t sig = 0;
            for (int i = 0; i < w * h; i += 97) sig = sig * 31 + img[i];
            if (sig != sig_prev) { distinct++; sig_prev = sig; if (!first_sig) first_sig = sig; }
            if (step == 300) save(argv[2], "demo_pelicula.ppm", img, w, h);
            last = img;
        }
    }
    psxdemo_stop();
    printf("pelicula: %dx%d, %d fotogramas distintos en 12 s (%.1f/s), %d bloques de audio sin datos\n", w, h, distinct,
           distinct / 12.0, starved);
    if (!last || distinct < 60 || starved > 2) fails++;
    if (psxdemo_start(&d, &p, PSXDEMO_TERRAIN, 6, 14000, -2200, -14000)) { puts("no arranca el terreno"); return 1; }
    const uint32_t *img = psxdemo_frame(&w, &h, 0.5f);
    int lit = 0;
    for (int i = 0; img && i < w * h; i++) lit += (img[i] & 0xFFFFFF) != (img[0] & 0xFFFFFF);   /* distinto del cielo (fondo) */
    printf("terreno: %dx%d, %d pixeles dibujados (sin contar el fondo)\n", w, h, lit);
    if (img) save(argv[2], "demo_terreno.ppm", img, w, h);
    if (!img || lit < 5000) fails++;
    psxdemo_stop();
    puts(fails ? "HAY FALLOS" : "TODO BIEN");
    return fails;
}
