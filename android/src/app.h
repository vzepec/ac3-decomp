/* Estado compartido de la rebanada vertical y API del render (render.c).
 * render.c no depende de Android: se prueba en el PC con test/test_render.c. */
#ifndef APP_H
#define APP_H

#include <GLES3/gl3.h>
#include <math.h>
#include <string.h>
#include "ace.h"
#include "scene.h"

#define FIRST_MISSION 6     /* registros de mision: 6..46 (tabla 0x800AF748 de ACE.BIN) */
#define LAST_MISSION 46
#define FIRST_PLANE 47      /* aviones del jugador: 47..68 (tabla 0x800AF79C) */
#define LAST_PLANE 68
#define VIEW_CELLS 16       /* radio de teselas dibujadas (16 x 2048 unidades) */

typedef struct { float x, y, z; } V3;

#define MAX_ENEMIES MAX_SPAWNS   /* aviones del guion (la ranura 0 es el jugador) */
#define MAX_MISSILES 24
#define MAX_PARTICLES 384
#define TRAIL_PTS 32                /* puntos por estela (como el juego) */
#define TICK (1.0f / 30.0f)         /* tick del juego: SIN RESOLVER (25 o 30 Hz); se usa 30 */

typedef struct {
    V3 pos, fwd, right, up, wp, home;
    float speed, bank, wp_timer;
    int mesh, alive, team;
} Enemy;

typedef struct {
    int alive;
} Ground;

/* Estela con bufer circular (docs/04-formatos.md, "Estelas de humo y de vapor"). */
typedef struct {
    V3 p[TRAIL_PTS][3];             /* misil: 3 posiciones (seccion triangular); ala: 1 */
    float age[TRAIL_PTS];
    unsigned char brk[TRAIL_PTS];   /* 1: no se une con el punto anterior (hubo pausa) */
    int head, count, stopped;       /* head = indice del punto mas nuevo */
    float emit_t;
} Trail;

typedef struct {
    V3 pos, dir;
    float speed, life;
    int target, active;             /* target: >= 0 avion, <= -2 objeto (-2 - indice) */
    Trail smoke;
} Missile;

enum { FX_EXPLOSION, FX_SMOKE_DARK, FX_SMOKE_LIGHT, FX_FIRE_DEBRIS, FX_SPARK };

typedef struct {
    V3 pos, vel;
    float age, life, size, size_end, grav, smoke_t;
    int kind, active;
} Particle;

typedef struct {
    void *platform;        /* struct android_app * en Android */
    int w, h;
    /* GL */
    GLuint prog, vram_tex, vao_tiles, vbo_tiles, vao_plane, vbo_plane;
    GLuint vao_model[MAX_MODELS], vbo_model[MAX_MODELS], vao_missile, vbo_missile;
    GLuint vao_part[10], vbo_part[10];      /* alas 0-4, dorsal 5-6, deriva 7-9 */
    GLuint vao_fx, vbo_fx;                  /* sprites de efectos (Vtx, por fotograma) */
    GLuint prog_solid, vao_solid, vbo_solid;
    GLuint prog_col, vao_col, vbo_col;      /* color por vertice: cielo y estelas */
    GLint us_mvp, us_color, uc_mvp;
    GLint u_mvp, u_model, u_cam, u_lit, u_light, u_fogcol, u_fog, u_alpha;
    /* datos */
    Disc disc;
    AcePack pack;
    Scene *scene;
    int status;            /* 0 cargando, 1 listo, -1 sin disco, -2 error */
    uint32_t mission, plane;
    int reload_mission, reload_plane, loaded_gl;
    /* vuelo */
    V3 pos, right, up, fwd;
    float speed;
    /* mando */
    float lx, ly, rx, ry, lt, rt;
    int l1, r1, sel, start, fire, fire_prev;
    float cam_yaw, cam_pitch;
    /* combate */
    Enemy enemies[MAX_ENEMIES];
    Ground ground[MAX_SPAWNS];
    Missile missiles[MAX_MISSILES];
    Particle parts[MAX_PARTICLES];
    Trail wing_trail[MAX_ENEMIES][2];       /* ranura 0 = jugador */
    int target, kills, fire_side, ndead_wait;
    float respawn_t;
    float fire_cd;
    uint32_t rng;
    /* tiempo y contador de fps */
    float fps_shown;
    double fps_hud_t;
    int fps_hud_frames;
    double last, fps_t;
    int frames;
} App;


void gl_init(App *a);
void gl_upload(App *a);
void reset_flight(App *a);
void update(App *a, float dt);
void draw(App *a);

#endif
