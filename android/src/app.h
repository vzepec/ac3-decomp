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

#define MAX_ENEMIES 8
#define MAX_MISSILES 24
#define MAX_BOOMS 16

typedef struct {
    V3 pos, fwd, right, up, wp;
    float speed, bank, respawn, wp_timer;
    int type, alive;
} Enemy;

typedef struct {
    V3 pos, dir;
    float speed, life;
    int target, active;
} Missile;

typedef struct {
    V3 pos;
    float t;
    int active;
} Boom;


typedef struct {
    void *platform;        /* struct android_app * en Android */
    int w, h;
    /* GL */
    GLuint prog, vram_tex, vao_tiles, vbo_tiles, vao_plane, vbo_plane;
    GLuint vao_enemy[4], vbo_enemy[4], vao_missile, vbo_missile;
    GLuint prog_solid, vao_solid, vbo_solid;
    GLint us_mvp, us_color;
    GLint u_mvp, u_model, u_cam, u_lit, u_light, u_fogcol, u_fog;
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
    Missile missiles[MAX_MISSILES];
    Boom booms[MAX_BOOMS];
    int target, kills, fire_side;
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
