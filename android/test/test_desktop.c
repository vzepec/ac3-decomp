/* Prueba de escritorio del port en C (no se compila en el APK).
 *   cc -O2 -Isrc test/test_desktop.c src/ace.c src/scene.c -lm -o build/test_desktop
 *   build/test_desktop <imagen.bin> <registro_mision> <registro_avion>
 */
#include <stdio.h>
#include <stdlib.h>
#include "ace.h"
#include "scene.h"

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "uso: %s <imagen> <mision> <avion>\n", argv[0]); return 2; }
    Disc d; AcePack p;
    static Scene s;
    if (disc_open(&d, argv[1])) { puts("no se abre el disco"); return 1; }
    printf("volumen AC3: %d\n", disc_volume_is_ac3(&d));
    if (pack_open(&d, &p)) { puts("no se abre el BPB"); return 1; }
    printf("entradas BPB: %u\n", p.count);
    scene_init(&s);
    int rm = scene_load_mission(&s, &d, &p, atoi(argv[2]));
    int tims_mission = s.tims_loaded;
    printf("mision: %d  teselas %u  triangulos de terreno %u  TIM subidas %d\n", rm, s.ntiles, s.tiles.n / 3, tims_mission);
    int ra = scene_load_aircraft(&s, &d, &p, atoi(argv[3]));
    printf("avion: %d  triangulos %u  TIM subidas %d\n", ra, s.plane.n / 3, s.tims_loaded - tims_mission);
    unsigned long sum = 0;
    for (int i = 0; i < VRAM_W * VRAM_H; i++) sum += s.vram[i];
    printf("suma VRAM %lu  celda(128,128)=%#x  suelo(0,0)=%.1f\n", sum, s.tilemap[128 * 256 + 128], scene_ground(&s, 0, 0));
    scene_free(&s); pack_free(&p); disc_close(&d);
    return 0;
}
