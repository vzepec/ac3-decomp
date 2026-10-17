/* Prueba de escritorio de la musica XA de la rebanada (src/music.c).
 *   cc -O2 -Isrc -I../psx/include test/test_music.c src/ace.c src/music.c ../psx/src/xa.c -lpthread -o build/test_music
 *   build/test_music <imagen.bin> <registro_mision> <segundos> <salida.raw>
 * Escribe la musica que entrega el mezclador (s16 estereo 44100 Hz) sin volumen aplicado
 * (vol = 1). tools/psxcheck_music.py la compara con la decodificacion directa de la pista. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ace.h"
#include "music.h"

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "uso: %s <imagen> <registro> <segundos> <salida.raw>\n", argv[0]); return 2; }
    Disc d;
    if (disc_open(&d, argv[1])) { puts("disco"); return 1; }
    int entry = music_track_for_mission((uint32_t)atoi(argv[2]));
    if (music_start(&d, entry)) { puts("music_start"); return 1; }
    int total = atoi(argv[3]) * 44100, done = 0;
    static float acc[2 * 4410];
    FILE *f = fopen(argv[4], "wb");
    while (done < total) {
        struct timespec ts = {0, 30 * 1000000L};
        nanosleep(&ts, NULL);
        for (int i = 0; i < 2 * 4410; i++) acc[i] = 0;
        /* tomar solo lo que haya (music_mix no espera): se cuentan las muestras no nulas */
        music_mix(acc, 1323, 1.0f);   /* 30 ms */
        for (int i = 0; i < 2 * 1323; i++) { short v = (short)acc[i]; fwrite(&v, 2, 1, f); }
        done += 1323;
    }
    fclose(f);
    music_stop();
    printf("pista %d del SPH, %d s escritos\n", entry, total / 44100);
    return 0;
}
