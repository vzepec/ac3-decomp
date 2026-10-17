/* Prueba de los bancos de sonido y del mezclador (no entra en el APK).
 *   cc -O2 -Isrc -I../psx/include test/test_sound.c src/ace.c src/scene.c src/sound.c src/audio.c src/music.c ../psx/src/xa.c -lm -lpthread -o build/test_sound
 *   build/test_sound <imagen.bin> <mision> <avion> <ranura> <salida.wav> */
#include <stdio.h>
#include <stdlib.h>
#include "audio.h"
#include "scene.h"

static void wav(const char *path, const int16_t *pcm, int frames) {
    FILE *f = fopen(path, "wb");
    int bytes = frames * 4;
    int hdr[11] = {0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00020001, AUDIO_RATE, AUDIO_RATE * 4,
                   0x00100004, 0x61746164, bytes};
    fwrite(hdr, 4, 11, f);
    fwrite(pcm, 4, frames, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 6) return 2;
    Disc d; AcePack p;
    static Scene s;
    if (disc_open(&d, argv[1]) || pack_open(&d, &p)) return 1;
    scene_init(&s);
    scene_load_common(&s, &d, &p);
    scene_load_mission(&s, &d, &p, atoi(argv[2]));
    scene_load_aircraft(&s, &d, &p, atoi(argv[3]));
    int count = 0;
    for (int i = 0; i < SOUND_SLOTS; i++) {
        const Sound *o = &s.sounds[i];
        if (!o->pcm) continue;
        count++;
        if (i == 0x0E || i == 0x1B || i == 0x1C || i == 0x1D || i == 84 || i == 85 || i == 88)
            printf("ranura %3d (%#x): %6u muestras a %5.0f Hz = %.2f s, bucle %d (desde %u), vol %.2f\n", i, i, o->n,
                   o->rate, o->n / o->rate, o->loops, o->loop_start, o->vol);
    }
    printf("%d sonidos cargados\n", count);
    int slot = atoi(argv[4]);
    audio_init(s.sounds);
    audio_play(slot, 1, 0, 0);
    int frames = (int)(AUDIO_RATE * (s.sounds[slot].n / s.sounds[slot].rate + 0.2));
    int16_t *buf = malloc(frames * 4);
    audio_mix(buf, frames);
    wav(argv[5], buf, frames);
    printf("ranura %d mezclada en %s (%d muestras estereo)\n", slot, argv[5], frames);
    return 0;
}
