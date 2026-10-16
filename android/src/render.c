/* Render, camara y vuelo de la rebanada vertical (sin dependencias de Android). */
#include "app.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "AC3Slice", __VA_ARGS__)
#else
#define LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif

/* ------------------------------------------------------------------ math */

typedef struct { float m[16]; } Mat4;  /* columnas */

static V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 vnorm(V3 a) { float l = sqrtf(vdot(a, a)); return l > 1e-6f ? vmul(a, 1.0f / l) : a; }
/* rota v alrededor del eje unitario k un angulo t (Rodrigues) */
static V3 vrot(V3 v, V3 k, float t) {
    float c = cosf(t), s = sinf(t);
    return vadd(vadd(vmul(v, c), vmul(vcross(k, v), s)), vmul(k, vdot(k, v) * (1 - c)));
}

static Mat4 mmul(Mat4 a, Mat4 b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}

static Mat4 perspective(float fovy, float aspect, float n, float f) {
    Mat4 r;
    memset(&r, 0, sizeof r);
    float t = 1.0f / tanf(fovy / 2);
    r.m[0] = t / aspect; r.m[5] = t;
    r.m[10] = (f + n) / (n - f); r.m[11] = -1;
    r.m[14] = 2 * f * n / (n - f);
    return r;
}

static Mat4 lookat(V3 eye, V3 at, V3 up) {
    V3 f = vnorm(vsub(at, eye)), s = vnorm(vcross(f, up)), u = vcross(s, f);
    Mat4 r = {{s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
               -vdot(s, eye), -vdot(u, eye), vdot(f, eye), 1}};
    return r;
}

/* ------------------------------------------------------------------ GL */

static const char *VS =
    "#version 300 es\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec4 aCol;\n"
    "layout(location=3) in vec3 aNrm;\n"
    "layout(location=4) in uvec2 aPage;\n"
    "uniform mat4 uMVP, uModel;\n"
    "uniform vec3 uCam, uLight;\n"
    "uniform int uLit;\n"
    "uniform vec2 uFog;\n"
    "out vec2 vUV; flat out uvec2 vPage; out vec3 vCol; out float vFog;\n"
    "void main(){\n"
    "  vec4 w = uModel * vec4(aPos,1.0);\n"
    "  gl_Position = uMVP * w;\n"
    "  vUV = aUV; vPage = aPage;\n"
    "  vec3 c = aCol.rgb * (255.0/128.0);\n"
    "  if (uLit == 1 && dot(aNrm,aNrm) > 0.01) {\n"
    "    vec3 n = normalize(mat3(uModel) * aNrm);\n"
    "    float d = max(dot(n, uLight), 0.0);\n"
    "    c = vec3(0.55 + 0.6*d);\n"
    "  }\n"
    "  vCol = c;\n"
    "  vFog = clamp((length(w.xyz-uCam)-uFog.x)/(uFog.y-uFog.x), 0.0, 1.0);\n"
    "}\n";

static const char *FS =
    "#version 300 es\n"
    "precision highp float; precision highp int; precision highp usampler2D;\n"
    "uniform usampler2D uVram;\n"
    "uniform vec3 uFogCol;\n"
    "uniform float uAlpha;\n"
    "in vec2 vUV; flat in uvec2 vPage; in vec3 vCol; in float vFog;\n"
    "out vec4 o;\n"
    "void main(){\n"
    "  int tp = int(vPage.x), cl = int(vPage.y);\n"
    "  int tx = (tp & 15) * 64, ty = ((tp >> 4) & 1) * 256, depth = (tp >> 7) & 3;\n"
    "  int cx = (cl & 63) * 16, cy = (cl >> 6) & 511;\n"
    "  int u = int(floor(vUV.x)) & 255, v = int(floor(vUV.y)) & 255;\n"
    "  uint t;\n"
    "  if (depth == 0) {\n"
    "    uint hw = texelFetch(uVram, ivec2(tx + u/4, ty + v), 0).r;\n"
    "    uint idx = (hw >> uint((u & 3) * 4)) & 15u;\n"
    "    t = texelFetch(uVram, ivec2(cx + int(idx), cy), 0).r;\n"
    "  } else if (depth == 1) {\n"
    "    uint hw = texelFetch(uVram, ivec2(tx + u/2, ty + v), 0).r;\n"
    "    uint idx = (hw >> uint((u & 1) * 8)) & 255u;\n"
    "    t = texelFetch(uVram, ivec2(cx + int(idx), cy), 0).r;\n"
    "  } else {\n"
    "    t = texelFetch(uVram, ivec2(tx + u, ty + v), 0).r;\n"
    "  }\n"
    "  if (t == 0u) discard;\n"
    "  vec3 c = vec3(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u)) / 31.0;\n"
    "  c = clamp(c * vCol, 0.0, 1.0);\n"
    "  o = vec4(mix(c, uFogCol, vFog), uAlpha);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOG("error de shader: %s", log);
    }
    return s;
}

static void setup_vao(GLuint *vao, GLuint *vbo) {
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);
    glBindVertexArray(*vao);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void *)offsetof(Vtx, r));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)offsetof(Vtx, nx));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 2, GL_UNSIGNED_SHORT, sizeof(Vtx), (void *)offsetof(Vtx, tpage));
    glBindVertexArray(0);
}

/* Programa sencillo de color plano: HUD, explosiones, estelas. */
static const char *VS_SOLID =
    "#version 300 es\n"
    "layout(location=0) in vec3 aPos;\n"
    "uniform mat4 uMVP;\n"
    "void main(){ gl_Position = uMVP * vec4(aPos,1.0); }\n";
static const char *FS_SOLID =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "out vec4 o;\n"
    "void main(){ o = uColor; }\n";

/* Color por vertice: bandas del cielo y estelas (Gouraud sin textura, como el juego). */
static const char *VS_COL =
    "#version 300 es\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec4 aCol;\n"
    "uniform mat4 uMVP;\n"
    "out vec4 vCol;\n"
    "void main(){ gl_Position = uMVP * vec4(aPos,1.0); vCol = aCol; }\n";
static const char *FS_COL =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 vCol; out vec4 o;\n"
    "void main(){ o = vCol; }\n";

static GLuint link2(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    return p;
}

void gl_init(App *a) {
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    a->prog = glCreateProgram();
    glAttachShader(a->prog, vs);
    glAttachShader(a->prog, fs);
    glLinkProgram(a->prog);
    a->u_mvp = glGetUniformLocation(a->prog, "uMVP");
    a->u_model = glGetUniformLocation(a->prog, "uModel");
    a->u_cam = glGetUniformLocation(a->prog, "uCam");
    a->u_lit = glGetUniformLocation(a->prog, "uLit");
    a->u_light = glGetUniformLocation(a->prog, "uLight");
    a->u_fogcol = glGetUniformLocation(a->prog, "uFogCol");
    a->u_fog = glGetUniformLocation(a->prog, "uFog");
    a->u_alpha = glGetUniformLocation(a->prog, "uAlpha");
    glUseProgram(a->prog);
    glUniform1f(a->u_alpha, 1.0f);
    a->prog_col = link2(VS_COL, FS_COL);
    a->uc_mvp = glGetUniformLocation(a->prog_col, "uMVP");
    glGenVertexArrays(1, &a->vao_col);
    glGenBuffers(1, &a->vbo_col);
    glBindVertexArray(a->vao_col);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo_col);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 28, (void *)12);
    glBindVertexArray(0);
    GLuint vs2 = compile(GL_VERTEX_SHADER, VS_SOLID), fs2 = compile(GL_FRAGMENT_SHADER, FS_SOLID);
    a->prog_solid = glCreateProgram();
    glAttachShader(a->prog_solid, vs2);
    glAttachShader(a->prog_solid, fs2);
    glLinkProgram(a->prog_solid);
    a->us_mvp = glGetUniformLocation(a->prog_solid, "uMVP");
    a->us_color = glGetUniformLocation(a->prog_solid, "uColor");
    glGenTextures(1, &a->vram_tex);
    glBindTexture(GL_TEXTURE_2D, a->vram_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, VRAM_W, VRAM_H, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    setup_vao(&a->vao_tiles, &a->vbo_tiles);
    setup_vao(&a->vao_plane, &a->vbo_plane);
    for (int i = 0; i < MAX_MODELS; i++) setup_vao(&a->vao_model[i], &a->vbo_model[i]);
    for (int i = 0; i < 10; i++) setup_vao(&a->vao_part[i], &a->vbo_part[i]);
    setup_vao(&a->vao_missile, &a->vbo_missile);
    setup_vao(&a->vao_fx, &a->vbo_fx);
    glGenVertexArrays(1, &a->vao_solid);
    glGenBuffers(1, &a->vbo_solid);
    glBindVertexArray(a->vao_solid);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo_solid);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void *)0);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);  /* el PS1 descarta con NCLIP; aqui se dibujan las dos caras */
}

static void upload_mesh(GLuint vbo, const Mesh *m) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (m->n ? m->n : 1) * sizeof(Vtx), m->v, GL_STATIC_DRAW);
}

void gl_upload(App *a) {
    Scene *s = a->scene;
    glBindTexture(GL_TEXTURE_2D, a->vram_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VRAM_W, VRAM_H, GL_RED_INTEGER, GL_UNSIGNED_SHORT, s->vram);
    upload_mesh(a->vbo_tiles, &s->tiles);
    upload_mesh(a->vbo_plane, &s->plane);
    for (uint32_t i = 0; i < MAX_MODELS; i++) upload_mesh(a->vbo_model[i], &s->models[i]);
    for (int i = 0; i < 10; i++) {
        const Part *pt = i < 5 ? &s->wing : i < 7 ? &s->dorsal : &s->rudder;
        int k = i < 5 ? i : i < 7 ? i - 5 : i - 7;
        upload_mesh(a->vbo_part[i], &pt->state[k]);
    }
    upload_mesh(a->vbo_missile, &s->missile);
    a->loaded_gl = 1;
}

/* ------------------------------------------------------------------ combate */

static const V3 WORLD_UP = {0, 1, 0};

static float frand(App *a) {
    a->rng = a->rng * 1664525u + 1013904223u;
    return (a->rng >> 8) / 16777216.0f;
}

/* rumbo del guion -> direccion GL. PS1: rotacion Y de (0,0,1) = (sin, 0, cos); GL z = -z */
static V3 yaw_dir(float yaw) { return v3(sinf(yaw), 0, -cosf(yaw)); }

static void new_waypoint(App *a, Enemy *e) {
    /* vuela entre puntos alrededor de su posicion inicial; si el jugador esta cerca, alrededor de el */
    V3 c = e->home, d = vsub(a->pos, e->pos);
    if (vdot(d, d) < 14000.0f * 14000) c = a->pos;
    float ang = frand(a) * 6.2832f, r = 1500 + frand(a) * 5000;
    e->wp = v3(c.x + cosf(ang) * r, 0, c.z + sinf(ang) * r);
    float g = scene_ground(a->scene, e->wp.x, e->wp.z);
    e->wp.y = fmaxf(g + 900, c.y - 600 + frand(a) * 1200);
    e->wp_timer = 6 + frand(a) * 6;
}

static void spawn_enemies(App *a) {
    Scene *s = a->scene;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &a->enemies[i];
        memset(e, 0, sizeof *e);
        if (i == 0 || i >= s->nair || s->air[i].mesh < 0) continue;
        const Spawn *sp = &s->air[i];
        e->pos = e->home = v3(sp->x, sp->y, sp->z);
        float g = scene_ground(s, e->pos.x, e->pos.z) + 300;
        if (e->pos.y < g) e->pos.y = e->home.y = g;
        e->fwd = yaw_dir(sp->yaw);
        e->right = vnorm(vcross(e->fwd, WORLD_UP));
        e->up = vcross(e->right, e->fwd);
        e->speed = 380 + frand(a) * 160;
        e->mesh = sp->mesh;
        e->team = sp->team;
        e->alive = 1;
        new_waypoint(a, e);
    }
    for (int i = 0; i < s->nobj; i++) a->ground[i].alive = s->obj[i].mesh >= 0;
}

/* Coloca al jugador en el inicio del guion (sin tocar enemigos ni objetos). */
static void player_respawn(App *a) {
    Scene *s = a->scene;
    /* inicio del jugador: registro 0 de la tabla de aviones del guion */
    float yaw = 0;
    if (s->nair > 0) { a->pos = v3(s->air[0].x, s->air[0].y, s->air[0].z); yaw = s->air[0].yaw; }
    else a->pos = v3(1024, 0, -1024);
    float g = scene_ground(s, a->pos.x, a->pos.z);
    if (a->pos.y < g + 800) a->pos.y = g + 2500;
    a->fwd = yaw_dir(yaw);
    a->right = vnorm(vcross(a->fwd, WORLD_UP));
    a->up = vcross(a->right, a->fwd);
    a->speed = 450;
    a->cam_yaw = a->cam_pitch = 0;
    a->crash_t = 0;
    memset(a->wing_trail[0], 0, sizeof a->wing_trail[0]);
}

void reset_flight(App *a) {
    if (!a->rng) a->rng = 12345;
    player_respawn(a);
    memset(a->missiles, 0, sizeof a->missiles);
    memset(a->parts, 0, sizeof a->parts);
    memset(a->wing_trail, 0, sizeof a->wing_trail);
    spawn_enemies(a);
    a->target = -1;
    a->respawn_t = 0;
}

static Particle *add_particle(App *a, int kind, V3 pos, V3 vel, float life, float size, float size_end) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &a->parts[i];
        if (p->active) continue;
        memset(p, 0, sizeof *p);
        p->kind = kind; p->pos = pos; p->vel = vel; p->life = life; p->size = size; p->size_end = size_end;
        p->active = 1;
        return p;
    }
    return NULL;
}

/* Impacto de misil (FUN_8007F15C): 1 explosion (vida 14 ticks, tamano 256, aditiva) + restos:
 * 2 fragmentos ardiendo con gravedad y 6 chispas. El destello del deposito 0x33C0 no esta
 * resuelto y no se dibuja. Las velocidades de los restos son aproximadas. */
static void missile_impact(App *a, V3 p, int big) {
    add_particle(a, FX_EXPLOSION, p, v3(0, 0, 0), 14 * TICK, 256, 256);
    if (big) {  /* derribo: explosion grande extra (FUN_8007EAF8: vida 11, tamano 120-247) */
        add_particle(a, FX_EXPLOSION, vadd(p, v3(30, 20, 0)), v3(0, 0, 0), 11 * TICK, 120 + frand(a) * 127, 120 + frand(a) * 127);
    }
    for (int i = 0; i < 8; i++) {
        V3 v = v3((frand(a) - 0.5f) * 600, frand(a) * 400 + 100, (frand(a) - 0.5f) * 600);
        Particle *q = add_particle(a, i < 2 ? FX_FIRE_DEBRIS : FX_SPARK, p, v, i < 2 ? 1.6f : 0.6f + frand(a) * 0.4f,
                                   i < 2 ? 60 : 14, i < 2 ? 40 : 6);
        if (q) q->grav = 0x8AA / 256.0f * 30 * 30 / 30;  /* gravedad del juego: +0x8AA/256 u/tick por tick */
    }
}

/* solo para test/test_render.c: provoca un impacto en un punto */
void test_boom(App *a, V3 p) { missile_impact(a, p, 1); }

static void update_particles(App *a, float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &a->parts[i];
        if (!p->active) continue;
        p->age += dt;
        if (p->age >= p->life) { p->active = 0; continue; }
        p->vel.y -= p->grav * dt;
        p->pos = vadd(p->pos, vmul(p->vel, dt));
        if (p->kind == FX_FIRE_DEBRIS && (p->smoke_t -= dt) <= 0) {
            p->smoke_t = 4 * TICK;
            add_particle(a, FX_SMOKE_DARK, p->pos, v3(0, 20, 0), 40 * TICK, 50, 100);
        }
    }
}

/* Estela: un punto nuevo cada `every` segundos; entre medias se actualiza el mas nuevo. */
static void trail_feed(Trail *t, const V3 *pos, int npos, float every, float dt) {
    t->emit_t -= dt;
    if (t->count == 0 || t->emit_t <= 0) {
        t->emit_t = every;
        t->head = (t->head + 1) % TRAIL_PTS;
        if (t->count < TRAIL_PTS) t->count++;
        t->age[t->head] = 0;
        t->brk[t->head] = (unsigned char)t->stopped;
        t->stopped = 0;
    }
    for (int k = 0; k < npos; k++) t->p[t->head][k] = pos[k];
}

static void trail_age(Trail *t, float life, float dt) {
    for (int i = 0; i < t->count; i++) t->age[(t->head - i + TRAIL_PTS) % TRAIL_PTS] += dt;
    while (t->count > 0 && t->age[(t->head - t->count + 1 + TRAIL_PTS) % TRAIL_PTS] > life) t->count--;
}

static void update_enemies(App *a, float dt) {
    int alive = 0, total = 0;
    for (int i = 1; i < MAX_ENEMIES; i++) {
        Enemy *e = &a->enemies[i];
        if (e->mesh >= 0 && i < a->scene->nair && a->scene->air[i].mesh >= 0) total++;
        if (!e->alive) continue;
        alive++;
        V3 to = vsub(e->wp, e->pos);
        e->wp_timer -= dt;
        if (vdot(to, to) < 700.0f * 700 || e->wp_timer <= 0) new_waypoint(a, e);
        V3 desired = vnorm(to);
        float side = vdot(vcross(e->fwd, desired), WORLD_UP);
        float k = fminf(1.0f, 0.8f * dt);
        e->fwd = vnorm(vadd(e->fwd, vmul(vsub(desired, e->fwd), k)));
        float bank_target = fmaxf(-1.0f, fminf(1.0f, side * 4.0f)) * 0.9f;
        e->bank += (bank_target - e->bank) * fminf(1.0f, 2.0f * dt);
        V3 r0 = vnorm(vcross(e->fwd, WORLD_UP)), u0 = vcross(r0, e->fwd);
        e->right = vrot(r0, e->fwd, e->bank);
        e->up = vrot(u0, e->fwd, e->bank);
        e->pos = vadd(e->pos, vmul(e->fwd, e->speed * dt));
        float g = scene_ground(a->scene, e->pos.x, e->pos.z) + 300;
        if (e->pos.y < g) e->pos.y = g;
    }
    /* todos derribados: vuelven a sus posiciones del guion a los 6 s */
    if (total && !alive) {
        if ((a->respawn_t += dt) > 6) { a->respawn_t = 0; spawn_enemies(a); }
    }
}

static V3 ground_pos(App *a, int i) {
    const Spawn *o = &a->scene->obj[i];
    return v3(o->x, o->y, o->z);
}

static void update_combat(App *a, float dt) {
    Scene *s = a->scene;
    /* blanco: lo mas centrado en un cono de ~35 grados delante (aviones y objetos enemigos) */
    a->target = -1;
    float best = 0.82f;
    for (int i = 1; i < MAX_ENEMIES; i++) {
        Enemy *e = &a->enemies[i];
        if (!e->alive || e->team == 0) continue;
        V3 d = vsub(e->pos, a->pos);
        float dist = sqrtf(vdot(d, d));
        if (dist > 14000 || dist < 1) continue;
        float c = vdot(vmul(d, 1.0f / dist), a->fwd);
        if (c > best) { best = c; a->target = i; }
    }
    for (int i = 0; i < s->nobj; i++) {
        if (!a->ground[i].alive || s->obj[i].team == 0) continue;
        V3 d = vsub(ground_pos(a, i), a->pos);
        float dist = sqrtf(vdot(d, d));
        if (dist > 14000 || dist < 1) continue;
        float c = vdot(vmul(d, 1.0f / dist), a->fwd);
        if (c > best) { best = c; a->target = -2 - i; }
    }
    /* disparo: al pulsar, alternando ala */
    a->fire_cd -= dt;
    if (a->fire && !a->fire_prev && a->fire_cd <= 0 && a->crash_t <= 0) {
        for (int i = 0; i < MAX_MISSILES; i++) {
            Missile *m = &a->missiles[i];
            if (m->active || m->smoke.count) continue;
            float side = a->fire_side ? 0.5f : -0.5f;
            a->fire_side ^= 1;
            memset(m, 0, sizeof *m);
            m->pos = vadd(vadd(a->pos, vmul(a->right, side * s->wingspan)), vmul(a->up, -5));
            m->dir = a->fwd;
            m->speed = a->speed + 150;
            m->life = 7;
            m->target = a->target;
            m->active = 1;
            a->fire_cd = 0.3f;
            break;
        }
    }
    a->fire_prev = a->fire;
    for (int i = 0; i < MAX_MISSILES; i++) {
        Missile *m = &a->missiles[i];
        if (m->active) {
            m->speed = fminf(m->speed + 1500 * dt, 2700);
            V3 tp; int has = 0;
            if (m->target >= 0 && a->enemies[m->target].alive) { tp = a->enemies[m->target].pos; has = 1; }
            if (m->target <= -2 && a->ground[-2 - m->target].alive) { tp = ground_pos(a, -2 - m->target); has = 1; }
            if (has) {
                V3 desired = vnorm(vsub(tp, m->pos));
                m->dir = vnorm(vadd(m->dir, vmul(vsub(desired, m->dir), fminf(1.0f, 3.2f * dt))));
            }
            V3 prev = m->pos;
            m->pos = vadd(m->pos, vmul(m->dir, m->speed * dt));
            m->life -= dt;
            /* estela de humo (sistema A): 3 posiciones alrededor del eje, un punto cada 4 ticks
             * (desplazamientos del juego: 5 u detras, 5 u arriba y abajo a los lados) */
            V3 r = vnorm(vcross(m->dir, WORLD_UP)), u = vcross(r, m->dir), back = vmul(m->dir, -5);
            V3 ps[3] = {vadd(m->pos, vadd(back, vmul(u, 5))), vadd(m->pos, vadd(back, vadd(vmul(r, -5), vmul(u, -5)))),
                        vadd(m->pos, vadd(back, vadd(vmul(r, 5), vmul(u, -5))))};
            trail_feed(&m->smoke, ps, 3, 4 * TICK, dt);
            for (int j = 1; j < MAX_ENEMIES && m->active; j++) {
                Enemy *e = &a->enemies[j];
                if (!e->alive) continue;
                float rad = fmaxf(95.0f, s->models[e->mesh].radius * 1.5f);
                V3 d = vsub(e->pos, m->pos);
                if (vdot(d, d) < rad * rad) {
                    e->alive = 0;
                    missile_impact(a, e->pos, 1);
                    a->kills++;
                    m->active = 0;
                }
            }
            for (int j = 0; j < s->nobj && m->active; j++) {
                if (!a->ground[j].alive) continue;
                float rad = fmaxf(95.0f, s->models[s->obj[j].mesh].radius * 1.2f);
                V3 d = vsub(ground_pos(a, j), m->pos);
                if (vdot(d, d) < rad * rad) {
                    a->ground[j].alive = 0;
                    missile_impact(a, ground_pos(a, j), 1);
                    a->kills++;
                    m->active = 0;
                }
            }
            float q0[3] = {prev.x, prev.y, prev.z}, q1[3] = {m->pos.x, m->pos.y, m->pos.z};
            if (m->active && scene_hit_segment(s, q0, q1)) { missile_impact(a, m->pos, 0); m->active = 0; }
            if (m->life <= 0) m->active = 0;
        }
        trail_age(&m->smoke, 90 * TICK, dt);   /* vida de 90 ticks */
    }
    update_particles(a, dt);
}

/* Puntas de ala (sistema B): un punto cada 2 ticks, vida 63 ticks. Se emite en giros cerrados
 * (la condicion real, bandera +5 & 8 de la entidad, esta SIN RESOLVER). */
static void wing_tip(const Mesh *m, V3 *tip) {
    float best = 0; *tip = v3(0, 0, 0);
    for (uint32_t i = 0; i < m->n; i++) if (m->v[i].x > best) { best = m->v[i].x; *tip = v3(m->v[i].x, m->v[i].y, m->v[i].z); }
}

static void update_wing_trails(App *a, float dt, float pull) {
    Scene *s = a->scene;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        V3 pos, right, up, fwd, tip;
        int emit;
        if (i == 0) {
            pos = a->pos; right = a->right; up = a->up; fwd = a->fwd;
            tip = v3(s->wingspan, 0, 0);
            if (s->wing.nstates) wing_tip(&s->wing.state[0], &tip);
            emit = pull > 0.7f;
        } else {
            Enemy *e = &a->enemies[i];
            if (!e->alive) { trail_age(&a->wing_trail[i][0], 63 * TICK, dt); trail_age(&a->wing_trail[i][1], 63 * TICK, dt); continue; }
            pos = e->pos; right = e->right; up = e->up; fwd = e->fwd;
            wing_tip(&s->models[e->mesh], &tip);
            emit = fabsf(e->bank) > 0.75f;
        }
        for (int k = 0; k < 2; k++) {
            Trail *t = &a->wing_trail[i][k];
            float sx = k ? -tip.x : tip.x;
            /* espacio de modelo: morro a -z -> fwd = -z */
            V3 w = vadd(vadd(vadd(pos, vmul(right, sx)), vmul(up, tip.y)), vmul(fwd, -tip.z));
            if (emit) trail_feed(t, &w, 1, 2 * TICK, dt);
            else { t->emit_t = 0; t->stopped = 1; }   /* sin emitir, solo envejece y se desvanece */
            trail_age(t, 63 * TICK, dt);
        }
    }
}

/* ------------------------------------------------------------------ vuelo */

static float dz(float v) { return fabsf(v) < 0.12f ? 0 : v; }

/* Choque con un objeto de tierra: el punto pasa al espacio del objeto (sin su rumbo) y se
 * compara con su caja envolvente ampliada en `r`. */
static int hit_object(App *a, int i, V3 p, float r) {
    Scene *s = a->scene;
    const Spawn *o = &s->obj[i];
    int mi = a->ground[i].alive ? o->mesh : o->mesh_dead;
    if (mi < 0) return 0;
    const Mesh *m = &s->models[mi];
    V3 d = vsub(p, v3(o->x, o->y, o->z));
    if (vdot(d, d) > (m->radius + r) * (m->radius + r) * 3) return 0;
    V3 f = yaw_dir(o->yaw), rt = vnorm(vcross(f, WORLD_UP));
    /* basis(): x = derecha, y = arriba, z = -adelante */
    float l[3] = {vdot(d, rt), d.y, -vdot(d, f)};
    for (int k = 0; k < 3; k++)
        if (l[k] < m->bmin[k] - r || l[k] > m->bmax[k] + r) return 0;
    return 1;
}

static void player_crash(App *a) {
    missile_impact(a, a->pos, 1);
    a->crash_t = 3.0f;
    a->crashes++;
    a->speed = 0;
}

/* Choques del jugador: terreno (triangulos: suelo, puentes, edificios), objetos y aviones.
 * Radio del jugador: 8 unidades (aprox. el fuselaje); la colision real del juego esta SIN RESOLVER. */
static void player_collisions(App *a, V3 prev) {
    Scene *s = a->scene;
    const float R = 8.0f;
    float q0[3] = {prev.x, prev.y, prev.z}, q1[3] = {a->pos.x, a->pos.y, a->pos.z};
    if (scene_hit_segment(s, q0, q1) || scene_hit_sphere(s, a->pos.x, a->pos.y, a->pos.z, R)) { player_crash(a); return; }
    for (int i = 0; i < s->nobj; i++)
        if (hit_object(a, i, a->pos, R)) { player_crash(a); return; }
    for (int i = 1; i < MAX_ENEMIES; i++) {
        Enemy *e = &a->enemies[i];
        if (!e->alive) continue;
        float rr = s->models[e->mesh].radius * 0.6f + R;
        V3 d = vsub(e->pos, a->pos);
        if (vdot(d, d) < rr * rr) {
            e->alive = 0;
            missile_impact(a, e->pos, 1);
            player_crash(a);
            return;
        }
    }
    /* misiones sin terreno (23, 43): suelo de seguridad, sin choque */
    if (!s->ntiles && a->pos.y < -1500) a->pos.y = -1500;
}

void update(App *a, float dt) {
    if (a->crash_t > 0) {  /* estrellado: solo siguen el mundo y los efectos */
        update_enemies(a, dt);
        update_combat(a, dt);
        update_wing_trails(a, dt, 0);
        if ((a->crash_t -= dt) <= 0) player_respawn(a);
        return;
    }
    float pitch = dz(a->ly), roll = dz(a->lx), yaw = (float)(a->r1 - a->l1);
    /* controles invertidos, como el original: palanca hacia delante (ly < 0) = morro abajo */
    float pr = pitch * 1.6f * dt, rr = roll * 2.8f * dt, yr = yaw * 0.6f * dt;
    a->up = vrot(a->up, a->right, pr);
    a->fwd = vrot(a->fwd, a->right, pr);
    a->right = vrot(a->right, a->fwd, rr);
    a->up = vrot(a->up, a->fwd, rr);
    a->fwd = vrot(a->fwd, a->up, -yr);
    a->right = vrot(a->right, a->up, -yr);
    a->fwd = vnorm(a->fwd);
    a->right = vnorm(vcross(a->fwd, a->up));
    a->up = vcross(a->right, a->fwd);
    a->speed += (a->rt - a->lt) * 500 * dt;
    if (a->speed < 150) a->speed = 150;
    if (a->speed > 1800) a->speed = 1800;
    V3 prev = a->pos;
    a->pos = vadd(a->pos, vmul(a->fwd, a->speed * dt));
    player_collisions(a, prev);
    if (a->crash_t > 0) return;
    a->cam_yaw = dz(a->rx) * 3.1f;
    a->cam_pitch = dz(a->ry) * 1.2f;
    update_enemies(a, dt);
    update_combat(a, dt);
    update_wing_trails(a, dt, fabsf(pitch));
}

/* ------------------------------------------------------------------ HUD */

/* Fuente propia de 5x7 para el HUD de depuracion (no es del juego). */
static const char FONT_CH[] = "0123456789FPSK";
static const uint8_t FONT[][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};

static float g_solid[6 * 3 * 4096];

static void draw_solid(App *a, const float *xyz, int nverts, const Mat4 *mvp, float r, float g, float b, float al) {
    if (!nverts) return;
    glUseProgram(a->prog_solid);
    glUniformMatrix4fv(a->us_mvp, 1, GL_FALSE, mvp->m);
    glUniform4f(a->us_color, r, g, b, al);
    glBindVertexArray(a->vao_solid);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo_solid);
    glBufferData(GL_ARRAY_BUFFER, nverts * 12, xyz, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, nverts);
}

/* rectangulo en coordenadas de pantalla (pixeles) -> 6 vertices */
static int push_rect(float *o, int n, float x, float y, float w, float h) {
    float q[6][2] = {{x, y}, {x + w, y}, {x, y + h}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    for (int i = 0; i < 6; i++) { o[(n + i) * 3] = q[i][0]; o[(n + i) * 3 + 1] = q[i][1]; o[(n + i) * 3 + 2] = 0; }
    return n + 6;
}

static int push_text(float *o, int n, const char *t, float x, float y, float px) {
    for (; *t; t++, x += 6 * px) {
        const char *c = strchr(FONT_CH, *t);
        if (!c || *t == ' ') continue;
        const uint8_t *g = FONT[c - FONT_CH];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col)) n = push_rect(o, n, x + col * px, y + row * px, px, px);
    }
    return n;
}

static void draw_hud(App *a, const Mat4 *vp) {
    Mat4 ortho = {{2.0f / a->w, 0, 0, 0, 0, -2.0f / a->h, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1}};
    float px = a->h >= 900 ? 3.0f : 2.0f;
    char buf[32];
    int n = 0;
    /* fps: discreto, arriba a la derecha */
    snprintf(buf, sizeof buf, "%d FPS", (int)(a->fps_shown + 0.5f));
    n = push_text(g_solid, n, buf, a->w - (strlen(buf) * 6 + 2) * px, 2 * px, px);
    snprintf(buf, sizeof buf, "K %d", a->kills);
    n = push_text(g_solid, n, buf, 3 * px, 2 * px, px);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_solid(a, g_solid, n, &ortho, 0.9f, 0.95f, 0.9f, 0.55f);
    /* marcador del blanco fijado */
    if (a->target != -1) {
        V3 p = a->target >= 0 ? a->enemies[a->target].pos : ground_pos(a, -2 - a->target);
        float cx = vp->m[0] * p.x + vp->m[4] * p.y + vp->m[8] * p.z + vp->m[12];
        float cy = vp->m[1] * p.x + vp->m[5] * p.y + vp->m[9] * p.z + vp->m[13];
        float cw = vp->m[3] * p.x + vp->m[7] * p.y + vp->m[11] * p.z + vp->m[15];
        if (cw > 1) {
            float sx = (cx / cw * 0.5f + 0.5f) * a->w, sy = (1 - (cy / cw * 0.5f + 0.5f)) * a->h;
            float h = 9 * px, t = px;
            n = 0;
            n = push_rect(g_solid, n, sx - h, sy - h, 2 * h, t);
            n = push_rect(g_solid, n, sx - h, sy + h - t, 2 * h, t);
            n = push_rect(g_solid, n, sx - h, sy - h, t, 2 * h);
            n = push_rect(g_solid, n, sx + h - t, sy - h, t, 2 * h);
            draw_solid(a, g_solid, n, &ortho, 0.3f, 1.0f, 0.4f, 0.9f);
        }
    }
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

/* ------------------------------------------------------------------ dibujo */

static Mat4 basis(V3 right, V3 up, V3 fwd, V3 pos) {
    /* el morro de los modelos mira a -z */
    Mat4 m = {{right.x, right.y, right.z, 0, up.x, up.y, up.z, 0, -fwd.x, -fwd.y, -fwd.z, 0, pos.x, pos.y, pos.z, 1}};
    return m;
}

static void draw_mesh(App *a, GLuint vao, uint32_t n, Mat4 m, int lit) {
    if (!n) return;
    glUniformMatrix4fv(a->u_model, 1, GL_FALSE, m.m);
    glUniform1i(a->u_lit, lit);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, n);
}

/* ---- buffer de color por vertice (cielo, estelas) ---- */

static float g_col[7 * 24576];
static int g_ncol;

static void col_vtx(V3 p, const float c[4]) {
    if (g_ncol >= 24576) return;
    float *o = g_col + 7 * g_ncol++;
    o[0] = p.x; o[1] = p.y; o[2] = p.z; o[3] = c[0]; o[4] = c[1]; o[5] = c[2]; o[6] = c[3];
}

static void col_quad(V3 p0, V3 p1, V3 p2, V3 p3, const float c01[4], const float c23[4]) {
    /* p0,p1 con color c01 (borde nuevo); p2,p3 con c23 */
    col_vtx(p0, c01); col_vtx(p1, c01); col_vtx(p2, c23);
    col_vtx(p1, c01); col_vtx(p3, c23); col_vtx(p2, c23);
}

static void col_flush(App *a, const Mat4 *mvp) {
    if (!g_ncol) return;
    glUseProgram(a->prog_col);
    glUniformMatrix4fv(a->uc_mvp, 1, GL_FALSE, mvp->m);
    glBindVertexArray(a->vao_col);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo_col);
    glBufferData(GL_ARRAY_BUFFER, g_ncol * 28, g_col, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_ncol);
    g_ncol = 0;
}

/* ---- cielo por bandas (FUN_80093E2C): anillos alrededor de la camara ---- */

static void lerp3(float o[4], const float a[3], const float b[3], float t) {
    for (int k = 0; k < 3; k++) o[k] = a[k] + (b[k] - a[k]) * t;
    o[3] = 1;
}

static void draw_sky(App *a, V3 eye, V3 view_fwd, float aspect, Mat4 view) {
    const Sky *k = &a->scene->sky;
    /* mezcla sol / espaldas una vez por fotograma segun el rumbo de la camara (como el juego) */
    float heading = atan2f(view_fwd.x, -view_fwd.z);
    float d = fabsf(remainderf(heading - k->sun_heading, 6.2831853f)) / 3.1415927f;
    float c[7][4];
    lerp3(c[0], k->zenith, k->zenith, 0);
    lerp3(c[1], k->zenith, k->zenith, 0);
    lerp3(c[2], k->high_sun, k->high_away, d);
    lerp3(c[3], k->low_sun, k->low_away, d);
    lerp3(c[4], k->fog, k->fog, 0);
    lerp3(c[5], k->fog, k->fog, 0);
    lerp3(c[6], k->below, k->below, 0);
    /* anillos: altura absoluta (GL y) y distancia horizontal (docs/04-formatos.md) */
    float alt = eye.y;
    float dh = 26624.0f * 26624.0f - alt * alt;
    float ring_y[7] = {k->band_alt[0] + 200000, k->band_alt[0], k->band_alt[1], k->band_alt[2], k->band_alt[3], 0, -60000};
    float ring_d[7] = {65534, 65534, 65534, 61440, 30720, dh > 0 ? sqrtf(dh) : 26624, 26624};
    const int SEG = 48;
    for (int r = 0; r < 6; r++) {
        for (int i = 0; i < SEG; i++) {
            float a0 = i * 6.2831853f / SEG, a1 = (i + 1) * 6.2831853f / SEG;
            V3 p0 = v3(eye.x + cosf(a0) * ring_d[r], ring_y[r], eye.z + sinf(a0) * ring_d[r]);
            V3 p1 = v3(eye.x + cosf(a1) * ring_d[r], ring_y[r], eye.z + sinf(a1) * ring_d[r]);
            V3 p2 = v3(eye.x + cosf(a0) * ring_d[r + 1], ring_y[r + 1], eye.z + sinf(a0) * ring_d[r + 1]);
            V3 p3 = v3(eye.x + cosf(a1) * ring_d[r + 1], ring_y[r + 1], eye.z + sinf(a1) * ring_d[r + 1]);
            col_quad(p0, p1, p2, p3, c[r], c[r + 1]);
        }
    }
    Mat4 proj = perspective(1.05f, aspect, 50.0f, 400000.0f);
    Mat4 vp = mmul(proj, view);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    col_flush(a, &vp);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

/* ---- sprites con textura de la VRAM (registro 0000) ---- */

static Mesh g_fx[3];    /* 0 aditivo, 1 sustractivo, 2 semitransparente medio */

static void fx_quad(int mode, V3 c, V3 axr, V3 axu, float u0, float v0, float uw, float vh, uint16_t tpage,
                    uint16_t clut, float shade) {
    Mesh *m = &g_fx[mode];
    if (m->n + 6 > m->cap) {
        m->cap = m->cap ? m->cap * 2 : 1536;
        m->v = realloc(m->v, m->cap * sizeof(Vtx));
    }
    V3 p[4] = {vsub(vsub(c, axr), axu), vsub(vadd(c, axr), axu), vadd(vsub(c, axr), axu), vadd(vadd(c, axr), axu)};
    float uv[4][2] = {{u0, v0 + vh}, {u0 + uw, v0 + vh}, {u0, v0}, {u0 + uw, v0}};
    int idx[6] = {0, 1, 2, 1, 3, 2};
    uint8_t col = (uint8_t)fminf(255, 131 * shade);   /* 0x83 del paquete GPU, atenuado */
    for (int i = 0; i < 6; i++) {
        Vtx *t = &m->v[m->n++];
        memset(t, 0, sizeof *t);
        t->x = p[idx[i]].x; t->y = p[idx[i]].y; t->z = p[idx[i]].z;
        t->u = uv[idx[i]][0]; t->v = uv[idx[i]][1];
        t->r = t->g = t->b = col; t->a = 255;
        t->tpage = tpage; t->clut = clut;
    }
}

static void fx_flush(App *a, const Mat4 *vp) {
    glUseProgram(a->prog);
    glUniformMatrix4fv(a->u_mvp, 1, GL_FALSE, vp->m);
    glUniform2f(a->u_fog, 1e7f, 2e7f);   /* sin niebla en los sprites */
    Mat4 id = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    glUniformMatrix4fv(a->u_model, 1, GL_FALSE, id.m);
    glUniform1i(a->u_lit, 0);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBindVertexArray(a->vao_fx);
    for (int mode = 0; mode < 3; mode++) {
        Mesh *m = &g_fx[mode];
        if (!m->n) continue;
        if (mode == 0) { glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE); glUniform1f(a->u_alpha, 1); }
        if (mode == 1) { glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); glBlendFunc(GL_ONE, GL_ONE); glUniform1f(a->u_alpha, 1); }
        if (mode == 2) { glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glUniform1f(a->u_alpha, 0.5f); }
        glBindBuffer(GL_ARRAY_BUFFER, a->vbo_fx);
        glBufferData(GL_ARRAY_BUFFER, m->n * sizeof(Vtx), m->v, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, m->n);
        m->n = 0;
    }
    glBlendEquation(GL_FUNC_ADD);
    glUniform1f(a->u_alpha, 1);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

/* Rejilla 4x4 de celdas de 64 px de 0000 s0 (tpage 0x17). Explosion: CLUT 0 (0x4016) y modo
 * aditivo; humo: CLUT 3 (0x40D6). Fotograma = edad * 10 / vida + 6 (FUN_80084324). */
static void fx_cell(int mode, V3 c, V3 cr, V3 cu, float size, int frame, uint16_t clut, float shade) {
    if (frame > 15) frame = 15;
    float h = size * 0.5f;
    fx_quad(mode, c, vmul(cr, h), vmul(cu, h), (frame & 3) * 64.0f, (frame >> 2) * 64.0f, 63.9f, 63.9f, 0x17, clut, shade);
}

static void draw_effects(App *a, const Mat4 *vp, V3 eye, V3 cr, V3 cu) {
    /* particulas */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &a->parts[i];
        if (!p->active) continue;
        float t = p->age / p->life;
        int frame = 6 + (int)(t * 10);
        float size = p->size + (p->size_end - p->size) * t;
        switch (p->kind) {
        case FX_EXPLOSION: fx_cell(0, p->pos, cr, cu, size, frame, 0x4016, 1); break;
        case FX_SMOKE_DARK: fx_cell(1, p->pos, cr, cu, size, frame, 0x40D6, 0.55f * (1 - t)); break;
        case FX_SMOKE_LIGHT: fx_cell(0, p->pos, cr, cu, size, frame, 0x40D6, 0.6f * (1 - t)); break;
        case FX_FIRE_DEBRIS: fx_cell(0, p->pos, cr, cu, size, 8 + ((int)(p->age * 15) & 3), 0x4016, 1 - t * 0.6f); break;
        case FX_SPARK: fx_cell(0, p->pos, cr, cu, size, 6, 0x4016, 1 - t); break;
        }
    }
    /* llama del misil (FUN_8008F388): estela alargada de 0000 s1 (16x64 texeles en u=0xB0,
     * v=0x60, tpage 0x18, CLUT 0x4496), estirada de la cola a la cabeza, aditiva */
    for (int i = 0; i < MAX_MISSILES; i++) {
        Missile *m = &a->missiles[i];
        if (!m->active) continue;
        V3 head = m->pos, tail = vsub(m->pos, vmul(m->dir, 26));
        V3 mid = vmul(vadd(head, tail), 0.5f);
        V3 side = vnorm(vcross(m->dir, vsub(eye, mid)));
        fx_quad(0, mid, vmul(side, 3.5f), vmul(m->dir, 13), 0xB0, 0x60, 15.9f, 63.9f, 0x18, 0x4496, 1);
    }
    fx_flush(a, vp);

    /* estelas: bandas Gouraud semitransparentes (FUN_8007A990 / FUN_8007B928). Color y modo de
     * mezcla SIN RESOLVER: gris claro con transparencia segun la edad. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (int i = 0; i < MAX_MISSILES; i++) {
        Trail *t = &a->missiles[i].smoke;
        for (int j = 0; j + 1 < t->count; j++) {
            int n0 = (t->head - j + TRAIL_PTS) % TRAIL_PTS, n1 = (t->head - j - 1 + TRAIL_PTS) % TRAIL_PTS;
            if (t->brk[n0]) continue;
            float c0[4] = {0.9f, 0.9f, 0.9f, 0.4f * (1 - t->age[n0] / (90 * TICK))};
            float c1[4] = {0.9f, 0.9f, 0.9f, 0.4f * (1 - t->age[n1] / (90 * TICK))};
            for (int s = 0; s < 3; s++)
                col_quad(t->p[n0][s], t->p[n0][(s + 1) % 3], t->p[n1][s], t->p[n1][(s + 1) % 3], c0, c1);
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++)
        for (int k = 0; k < 2; k++) {
            Trail *t = &a->wing_trail[i][k];
            for (int j = 0; j + 1 < t->count; j++) {
                int n0 = (t->head - j + TRAIL_PTS) % TRAIL_PTS, n1 = (t->head - j - 1 + TRAIL_PTS) % TRAIL_PTS;
                if (t->brk[n0]) continue;
                V3 p0 = t->p[n0][0], p1 = t->p[n1][0];
                V3 side = vmul(vnorm(vcross(vsub(p0, p1), vsub(eye, p0))), 1.2f);
                float c0[4] = {1, 1, 1, 0.5f * (1 - t->age[n0] / (63 * TICK))};
                float c1[4] = {1, 1, 1, 0.5f * (1 - t->age[n1] / (63 * TICK))};
                col_quad(vsub(p0, side), vadd(p0, side), vsub(p1, side), vadd(p1, side), c0, c1);
            }
        }
    col_flush(a, vp);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

/* estado de cada pieza movil segun los mandos (la logica real del juego esta SIN RESOLVER) */
static int wing_state(App *a) {
    Scene *s = a->scene;
    float roll = dz(a->lx), pitch = dz(a->ly);
    if (roll > 0.35f && s->wing_roll_right) return s->wing_roll_right;
    if (roll < -0.35f && s->wing_roll_left) return s->wing_roll_left;
    if (pitch > 0.35f && s->wing_nose_up) return s->wing_nose_up;
    if (pitch < -0.35f && s->wing_nose_down) return s->wing_nose_down;
    return 0;
}

void draw(App *a) {
    if (a->status != 1) {
        float c[3] = {0.05f, 0.05f, 0.25f};
        if (a->status == -2) { c[0] = 0.6f; c[1] = 0.0f; c[2] = 0.0f; }
        glClearColor(c[0], c[1], c[2], 1);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    Scene *s = a->scene;
    const float *fog = s->sky.ok ? s->sky.fog : (const float[3]){0.55f, 0.68f, 0.85f};
    glViewport(0, 0, a->w, a->h);
    glClearColor(fog[0], fog[1], fog[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* camara de persecucion cercana, orbitable con la palanca derecha */
    V3 back = vmul(a->fwd, -1);
    back = vrot(back, a->up, a->cam_yaw);
    back = vrot(back, a->right, a->cam_pitch);
    V3 eye = vadd(vadd(a->pos, vmul(back, 125)), vmul(a->up, 30));
    V3 look = vadd(a->pos, vmul(a->fwd, 30));
    Mat4 view = lookat(eye, look, a->up);
    float aspect = (float)a->w / a->h;
    Mat4 proj = perspective(1.05f, aspect, 3.0f, 2048.0f * (VIEW_CELLS + 2));
    Mat4 vp = mmul(proj, view);
    if (s->sky.ok) draw_sky(a, eye, vnorm(vsub(look, eye)), aspect, view);

    glUseProgram(a->prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, a->vram_tex);
    glUniformMatrix4fv(a->u_mvp, 1, GL_FALSE, vp.m);
    glUniform3f(a->u_cam, eye.x, eye.y, eye.z);
    glUniform3f(a->u_fogcol, fog[0], fog[1], fog[2]);
    /* niebla: el mundo acaba en el radio 0x6800 = 26624 (anillo del horizonte del cielo) */
    glUniform2f(a->u_fog, 7000.0f, 26624.0f);
    glUniform1f(a->u_alpha, 1);
    V3 light = vnorm(v3(0.4f, 0.85f, 0.3f));
    glUniform3f(a->u_light, light.x, light.y, light.z);

    /* terreno: una llamada por celda visible */
    glUniform1i(a->u_lit, 0);
    glBindVertexArray(a->vao_tiles);
    int ccx = (int)floorf(a->pos.x / 2048.0f) + 128, ccz = (int)floorf(-a->pos.z / 2048.0f) + 128;
    for (int cz = ccz - VIEW_CELLS; cz <= ccz + VIEW_CELLS; cz++) {
        if (cz < 0 || cz > 255) continue;
        for (int cx = ccx - VIEW_CELLS; cx <= ccx + VIEW_CELLS; cx++) {
            if (cx < 0 || cx > 255) continue;
            uint32_t t = s->tilemap[cz * 256 + cx] & 0x1FF;
            if (!t || t > s->ntiles || !s->tile_count[t]) continue;
            /* centro de la celda: PS1 ((c-128)*2048 + 1024), GL z = -z */
            float ox = (cx - 128) * 2048.0f + 1024, oz = -((cz - 128) * 2048.0f + 1024);
            Mat4 m = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, ox, 0, oz, 1}};
            glUniformMatrix4fv(a->u_model, 1, GL_FALSE, m.m);
            glDrawArrays(GL_TRIANGLES, s->tile_first[t], s->tile_count[t]);
        }
    }

    /* objetos de tierra del guion (s4), con su rumbo; destruidos: modelo anterior (hipotesis) */
    for (int i = 0; i < s->nobj; i++) {
        const Spawn *o = &s->obj[i];
        int mi = a->ground[i].alive ? o->mesh : o->mesh_dead;
        if (mi < 0) continue;
        V3 f = yaw_dir(o->yaw), r = vnorm(vcross(f, WORLD_UP));
        V3 d = vsub(v3(o->x, o->y, o->z), eye);
        if (vdot(d, d) > 30000.0f * 30000) continue;
        draw_mesh(a, a->vao_model[mi], s->models[mi].n, basis(r, WORLD_UP, f, v3(o->x, o->y, o->z)), 0);
    }
    /* aviones del guion (LOD de s2, colores precalculados) */
    for (int i = 1; i < MAX_ENEMIES; i++) {
        Enemy *e = &a->enemies[i];
        if (e->alive) draw_mesh(a, a->vao_model[e->mesh], s->models[e->mesh].n, basis(e->right, e->up, e->fwd, e->pos), 0);
    }
    /* misiles */
    for (int i = 0; i < MAX_MISSILES; i++) {
        Missile *m = &a->missiles[i];
        if (!m->active) continue;
        V3 r = vnorm(vcross(m->dir, WORLD_UP)), u = vcross(r, m->dir);
        draw_mesh(a, a->vao_missile, s->missile.n, basis(r, u, m->dir, m->pos), 0);
    }
    /* avion del jugador: piezas fijas + estado de alas, aerofreno y timon */
    Mat4 pm = basis(a->right, a->up, a->fwd, a->pos);
    if (a->crash_t <= 0) draw_mesh(a, a->vao_plane, s->plane.n, pm, 1);
    if (s->wing.nstates == 5 && a->crash_t <= 0) {
        int w = wing_state(a);
        draw_mesh(a, a->vao_part[w], s->wing.state[w].n, pm, 1);
        int db = a->lt > 0.4f ? 1 : 0;
        draw_mesh(a, a->vao_part[5 + db], s->dorsal.state[db].n, pm, 1);
        int ru = a->r1 ? s->rudder_right : a->l1 ? s->rudder_left : 0;
        draw_mesh(a, a->vao_part[7 + ru], s->rudder.state[ru].n, pm, 1);
    }
    glBindVertexArray(0);

    /* efectos: sprites del registro 0000 orientados a la camara, y estelas */
    V3 cf = vnorm(vsub(look, eye));
    V3 cr = vnorm(vcross(cf, a->up)), cu = vcross(cr, cf);
    draw_effects(a, &vp, eye, cr, cu);

    draw_hud(a, &vp);
}
