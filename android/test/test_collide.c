/* Prueba de las colisiones contra los triangulos del terreno (no entra en el APK).
 *   cc -O2 -Isrc test/test_collide.c src/ace.c src/scene.c -lm -o build/test_collide
 *   build/test_collide <imagen.bin> <mision>
 * Busca columnas con hueco bajo una estructura (puentes, voladizos): de arriba abajo,
 * choque -> aire libre -> choque. Antes (altura maxima de la celda) eran impasables. */
#include <stdio.h>
#include <stdlib.h>
#include "ace.h"
#include "scene.h"

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    Disc d; AcePack p;
    static Scene s;
    if (disc_open(&d, argv[1]) || pack_open(&d, &p)) return 1;
    scene_init(&s);
    if (scene_load_mission(&s, &d, &p, atoi(argv[2]))) { puts("carga"); return 1; }
    const float R = 8;
    int columns = 0, gaps = 0, shown = 0;
    for (int cz = 0; cz < 256; cz++)
        for (int cx = 0; cx < 256; cx++) {
            uint32_t t = s.tilemap[cz * 256 + cx] & 0x1FF;
            if (!t || t > s.ntiles || s.tile_top[t] < 1300) continue;   /* solo celdas con relieve o estructuras */
            for (int sx = -3; sx <= 3; sx++)
                for (int sz = -3; sz <= 3; sz++) {
                    float x = (cx - 128) * 2048.0f + 1024 + sx * 290, z = -((cz - 128) * 2048.0f + 1024) + sz * 290;
                    columns++;
                    int state = 0;  /* 0 aire sobre todo, 1 dentro de algo, 2 aire debajo de algo */
                    float y_hit = 0, y_free = 0;
                    for (float y = s.tile_top[t] + 50; y > 0; y -= 4) {
                        int h = scene_hit_sphere(&s, x, y, z, R);
                        if (state == 0 && h) { state = 1; y_hit = y; }
                        else if (state == 1 && !h) { state = 2; y_free = y; }
                        else if (state == 2 && h) {
                            if (y_free - y > 40) {   /* hueco de mas de 40 unidades: se cabe con margen */
                                gaps++;
                                if (shown++ < 6)
                                    printf("celda (%d,%d) punto (%.0f, %.0f): choque a %.0f, hueco %.0f..%.0f, suelo %.0f; antes la celda era solida hasta %.0f\n",
                                           cx, cz, x, z, y_hit, y_free, y, y, s.tile_top[t]);
                            }
                            break;
                        }
                    }
                }
        }
    printf("mision %s: %d columnas probadas, %d con hueco bajo una estructura\n", argv[2], columns, gaps);
    /* control: en mar abierto (y lejos de todo), a 100 sobre el suelo no hay choque y a 2 si */
    return 0;
}
