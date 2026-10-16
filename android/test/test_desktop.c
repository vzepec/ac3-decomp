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
    printf("comun (registro 0000): %d  TIM %d\n", scene_load_common(&s, &d, &p), s.tims_loaded);
    printf("region: %s\n", s.pal ? "PAL (25 ticks/s)" : "NTSC (30 ticks/s)");
    s.tims_loaded = 0;
    int rm = scene_load_mission(&s, &d, &p, atoi(argv[2]));
    int tims_mission = s.tims_loaded;
    printf("mision: %d  teselas %u  triangulos de terreno %u  TIM subidas %d\n", rm, s.ntiles, s.tiles.n / 3, tims_mission);
    int ra = scene_load_aircraft(&s, &d, &p, atoi(argv[3]));
    const Sky *k = &s.sky;
    printf("cielo ok=%d cenit #%02x%02x%02x niebla #%02x%02x%02x bandas %.0f %.0f %.0f %.0f sol %d\n", k->ok,
           (int)(k->zenith[0] * 255 + .5f), (int)(k->zenith[1] * 255 + .5f), (int)(k->zenith[2] * 255 + .5f),
           (int)(k->fog[0] * 255 + .5f), (int)(k->fog[1] * 255 + .5f), (int)(k->fog[2] * 255 + .5f),
           k->band_alt[0], k->band_alt[1], k->band_alt[2], k->band_alt[3], k->sun);
    for (int i = 0; i < s.nair; i++) {
        const Spawn *o = &s.air[i];
        printf("  avion %2d bando %d tipo %#04x modelo %3d pos (%.0f, %.0f, %.0f) rumbo %.0f malla %d (%u tri, r %.0f)\n", i,
               o->team, o->type, o->model, o->x, o->y, o->z, o->yaw * 57.2958f, o->mesh,
               o->mesh >= 0 ? s.models[o->mesh].n / 3 : 0, o->mesh >= 0 ? s.models[o->mesh].radius : 0);
    }
    for (int i = 0; i < s.nobj; i++) {
        const Spawn *o = &s.obj[i];
        printf("  objeto %2d bando %d tipo %#04x modelo %3d pos (%.0f, %.0f, %.0f) suelo %.0f malla %d (%u tri)\n", i,
               o->team, o->type, o->model, o->x, o->y, o->z, scene_ground(&s, o->x, o->z), o->mesh,
               o->mesh >= 0 ? s.models[o->mesh].n / 3 : 0);
    }
    printf("misil: %u triangulos, r %.1f\n", s.missile.n / 3, s.missile.radius);
    printf("avion: %d  triangulos fijos %u  TIM subidas %d  envergadura %.0f\n", ra, s.plane.n / 3,
           s.tims_loaded - tims_mission, s.wingspan);
    printf("  piezas: alas %d estados, dorsal %d, deriva %d; alabeo D=%d I=%d morro arriba=%d abajo=%d timon D=%d I=%d\n",
           s.wing.nstates, s.dorsal.nstates, s.rudder.nstates, s.wing_roll_right, s.wing_roll_left, s.wing_nose_up,
           s.wing_nose_down, s.rudder_right, s.rudder_left);
    unsigned long sum = 0;
    for (int i = 0; i < VRAM_W * VRAM_H; i++) sum += s.vram[i];
    printf("suma VRAM %lu  celda(128,128)=%#x  suelo(0,0)=%.1f\n", sum, s.tilemap[128 * 256 + 128], scene_ground(&s, 0, 0));
    scene_free(&s); pack_free(&p); disc_close(&d);
    return 0;
}
