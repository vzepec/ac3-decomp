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


typedef struct {
    void *platform;        /* struct android_app * en Android */
    int w, h;
    /* GL */
    GLuint prog, vram_tex, vao_tiles, vbo_tiles, vao_plane, vbo_plane;
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
    int l1, r1;
    float cam_yaw, cam_pitch;
    /* tiempo */
    double last, fps_t;
    int frames;
} App;


void gl_init(App *a);
void gl_upload(App *a);
void reset_flight(App *a);
void update(App *a, float dt);
void draw(App *a);

#endif
