/* Escena de la rebanada vertical: VRAM simulada + mallas de terreno y avion.
 * Sin dependencias de OpenGL: se puede compilar y probar en el PC. */
#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>
#include "ace.h"

#define VRAM_W 1024
#define VRAM_H 512
#define MAX_TILES 512
#define MAX_MODELS 32       /* modelos distintos de aviones (LOD) y objetos (s4) por mision */
#define MAX_SPAWNS 32

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
    float radius;           /* max |coordenada| (para choques y estelas) */
    float bmin[3], bmax[3]; /* caja envolvente en espacio de modelo */
} Mesh;

/* Cielo y niebla: preludio del tipo 17 (docs/04-formatos.md, tools/acesky.py). */
typedef struct {
    float zenith[3], high_sun[3], high_away[3], low_sun[3], low_away[3], fog[3], below[3];
    float band_alt[4];      /* alturas (GL y) de los anillos 0-3; el 4 es el horizonte */
    float sun_heading;      /* rumbo del sol (preludio +0x3E, 4096 = 360) en radianes */
    float sun_elev;         /* elevacion del sol (preludio +0x3C, 4096 = 360) en radianes */
    float sun_dir[3];       /* direccion GL hacia el sol (FUN_80089A08: RotMatrixYXZ(rx, ry) * z) */
    int sun;                /* preludio +0x6C: byte bajo = disco del sol, alto = destello */
    int haze, weather, ok;
} Sky;

/* Registro de las tablas de aparicion del guion (s6): docs/04-formatos.md. */
typedef struct {
    int team, type, model;  /* model: id de LOD (aviones) o indice de s4 (objetos); -1 = sin modelo */
    float x, y, z;          /* GL */
    float yaw;              /* radianes (0x4000 = 360; adelante PS1 = (sin, cos); validado con las demos) */
    int mesh;               /* indice en Scene.models, -1 si no hay */
    int mesh_dead;          /* objetos: modelo anterior de s4 (hipotesis: version destruida), -1 si no */
} Spawn;

/* Pieza movil del avion del jugador: varios estados de vertices (tools/acesurf.py). */
typedef struct {
    Mesh state[5];
    int nstates;
} Part;

typedef struct {
    uint16_t vram[VRAM_W * VRAM_H];
    /* terreno */
    uint16_t tilemap[256 * 256];
    Mesh tiles;                         /* todas las teselas seguidas */
    uint32_t tile_first[MAX_TILES], tile_count[MAX_TILES];
    float tile_top[MAX_TILES];          /* altura maxima (GL y) de cada tesela */
    uint32_t ntiles;
    /* avion del jugador (espacio de modelo, morro hacia -z) */
    Mesh plane;                         /* piezas fijas (o todo, si no sigue el patron de 12) */
    Part wing, dorsal, rudder;          /* alas (neutra, alabeo D/I, morro arriba/abajo), aerofreno, timon */
    int wing_roll_right, wing_roll_left, wing_nose_up, wing_nose_down;  /* estado (indice) */
    int rudder_right, rudder_left;
    float wingspan;
    /* modelos de la mision: aviones (grupo LOD id*4 de s2/s3) y objetos (s4) */
    Mesh models[MAX_MODELS];
    int model_key[MAX_MODELS];          /* id | 0x100 si es objeto de s4 */
    uint32_t nmodels;
    Mesh missile;                       /* arma: grupo 0x18 (s3, entrada 0) o s3 entrada 3 */
    /* guion */
    Spawn air[MAX_SPAWNS], obj[MAX_SPAWNS];
    int nair, nobj;
    Sky sky;
    int tims_loaded;
    int pal;                            /* 1: disco europeo (PAL, 25 ticks/s); 0: NTSC (30) */
} Scene;

void scene_init(Scene *s);
void scene_free(Scene *s);
/* Texturas de efectos comunes (registro 0000: explosiones, humo, llamas). */
int scene_load_common(Scene *s, Disc *d, AcePack *p);
/* Carga un registro de mision (tipo 17): mapa, teselas, texturas, cielo, guion y modelos. */
int scene_load_mission(Scene *s, Disc *d, AcePack *p, uint32_t record);
/* Carga el avion del jugador (registro de tipo 4 = 47 + indice). */
int scene_load_aircraft(Scene *s, Disc *d, AcePack *p, uint32_t record);
/* Altura del suelo (GL y) aproximada en una posicion GL. */
float scene_ground(const Scene *s, float x, float z);
/* Choque de una esfera (GL) con los triangulos del terreno (teselas, incluidos puentes y
 * edificios). Devuelve 1 si toca alguno. */
int scene_hit_sphere(const Scene *s, float x, float y, float z, float r);
/* Choque de un segmento p0-p1 (GL) con los triangulos del terreno. */
int scene_hit_segment(const Scene *s, const float p0[3], const float p1[3]);
/* Libera los vertices de las mallas (para recargar). */
void scene_clear_meshes(Scene *s);

#endif
