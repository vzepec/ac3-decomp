/* Escena de la rebanada vertical: VRAM simulada + mallas de terreno y avion.
 * Sin dependencias de OpenGL: se puede compilar y probar en el PC. */
#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>
#include "ace.h"

#define VRAM_W 1024
#define VRAM_H 512
#define MAX_TILES 512

typedef struct {
    float x, y, z;          /* coordenadas GL: (x, -y, -z) de PS1 */
    float u, v;             /* UV en texeles de la pagina (0..255) */
    uint8_t r, g, b, a;     /* color de vertice PS1 (128 = neutro) */
    float nx, ny, nz;       /* normal (solo modelos iluminados) */
    uint16_t tpage, clut;   /* campos crudos del paquete GPU */
} Vtx;                      /* 40 bytes */

typedef struct {
    Vtx *v;
    uint32_t n, cap;
} Mesh;

typedef struct {
    uint16_t vram[VRAM_W * VRAM_H];
    /* terreno */
    uint16_t tilemap[256 * 256];
    Mesh tiles;                         /* todas las teselas seguidas */
    uint32_t tile_first[MAX_TILES], tile_count[MAX_TILES];
    float tile_top[MAX_TILES];          /* altura maxima (GL y) de cada tesela */
    uint32_t ntiles;
    /* avion del jugador (espacio de modelo, morro hacia -z) */
    Mesh plane;
    /* aviones de la mision (s2, LOD alto) y misil (s3, entrada 3) */
    Mesh enemy[4];
    uint32_t nenemy;
    Mesh missile;
    int tims_loaded;
} Scene;

void scene_init(Scene *s);
void scene_free(Scene *s);
/* Carga un registro de mision (tipo 17): mapa, teselas y texturas. */
int scene_load_mission(Scene *s, Disc *d, AcePack *p, uint32_t record);
/* Carga el avion del jugador (registro de tipo 4 = 47 + indice). */
int scene_load_aircraft(Scene *s, Disc *d, AcePack *p, uint32_t record);
/* Altura del suelo (GL y) aproximada en una posicion GL. */
float scene_ground(const Scene *s, float x, float z);

#endif
