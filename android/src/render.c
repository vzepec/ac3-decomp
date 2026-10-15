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
    "  o = vec4(mix(c, uFogCol, vFog), 1.0);\n"
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
    for (int i = 0; i < 4; i++) setup_vao(&a->vao_enemy[i], &a->vbo_enemy[i]);
    setup_vao(&a->vao_missile, &a->vbo_missile);
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
    for (int i = 0; i < 4; i++) upload_mesh(a->vbo_enemy[i], &s->enemy[i]);
    upload_mesh(a->vbo_missile, &s->missile);
    a->loaded_gl = 1;
}

/* ------------------------------------------------------------------ combate */

static const V3 WORLD_UP = {0, 1, 0};

static float frand(App *a) {
    a->rng = a->rng * 1664525u + 1013904223u;
    return (a->rng >> 8) / 16777216.0f;
}

static void new_waypoint(App *a, Enemy *e) {
    float ang = frand(a) * 6.2832f, r = 1500 + frand(a) * 4000;
    e->wp = v3(a->pos.x + cosf(ang) * r, 0, a->pos.z + sinf(ang) * r);
    float g = scene_ground(a->scene, e->wp.x, e->wp.z);
    e->wp.y = fmaxf(g + 900, a->pos.y - 600 + frand(a) * 1200);
    e->wp_timer = 6 + frand(a) * 6;
}

static void spawn_enemy(App *a, Enemy *e, int idx) {
    float ang = frand(a) * 6.2832f, r = 5000 + frand(a) * 4000;
    e->pos = v3(a->pos.x + cosf(ang) * r, 0, a->pos.z + sinf(ang) * r);
    e->pos.y = fmaxf(scene_ground(a->scene, e->pos.x, e->pos.z) + 900, a->pos.y - 400 + frand(a) * 800);
    float h = frand(a) * 6.2832f;
    e->fwd = v3(cosf(h), 0, sinf(h));
    e->right = vnorm(vcross(e->fwd, WORLD_UP));
    e->up = vcross(e->right, e->fwd);
    e->speed = 380 + frand(a) * 160;
    e->bank = 0;
    e->type = a->scene->nenemy ? idx % (int)a->scene->nenemy : 0;
    e->alive = a->scene->nenemy > 0;
    e->respawn = 0;
    new_waypoint(a, e);
}

void reset_flight(App *a) {
    a->pos = v3(1024, 0, -1024);
    a->pos.y = scene_ground(a->scene, a->pos.x, a->pos.z) + 2500;
    a->right = v3(1, 0, 0);
    a->up = v3(0, 1, 0);
    a->fwd = v3(0, 0, -1);
    a->speed = 450;
    a->cam_yaw = a->cam_pitch = 0;
    if (!a->rng) a->rng = 12345;
    memset(a->missiles, 0, sizeof a->missiles);
    memset(a->booms, 0, sizeof a->booms);
    for (int i = 0; i < MAX_ENEMIES; i++) spawn_enemy(a, &a->enemies[i], i);
    a->target = -1;
}

static void add_boom(App *a, V3 p) {
    for (int i = 0; i < MAX_BOOMS; i++)
        if (!a->booms[i].active) { a->booms[i].pos = p; a->booms[i].t = 0; a->booms[i].active = 1; return; }
}

static void update_enemies(App *a, float dt) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &a->enemies[i];
        if (!e->alive) {
            if (a->scene->nenemy && (e->respawn -= dt) <= 0) spawn_enemy(a, e, i);
            continue;
        }
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
        V3 d = vsub(e->pos, a->pos);
        if (vdot(d, d) > 16000.0f * 16000) spawn_enemy(a, e, i);  /* demasiado lejos: reaparece cerca */
    }
}

static void update_combat(App *a, float dt) {
    /* blanco: el enemigo vivo mas centrado en un cono de ~35 grados delante */
    a->target = -1;
    float best = 0.82f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &a->enemies[i];
        if (!e->alive) continue;
        V3 d = vsub(e->pos, a->pos);
        float dist = sqrtf(vdot(d, d));
        if (dist > 14000 || dist < 1) continue;
        float c = vdot(vmul(d, 1.0f / dist), a->fwd);
        if (c > best) { best = c; a->target = i; }
    }
    /* disparo: al pulsar, alternando ala */
    a->fire_cd -= dt;
    if (a->fire && !a->fire_prev && a->fire_cd <= 0) {
        for (int i = 0; i < MAX_MISSILES; i++) {
            Missile *m = &a->missiles[i];
            if (m->active) continue;
            float side = a->fire_side ? 14.0f : -14.0f;
            a->fire_side ^= 1;
            m->pos = vadd(vadd(a->pos, vmul(a->right, side)), vmul(a->up, -5));
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
        if (!m->active) continue;
        m->speed = fminf(m->speed + 1500 * dt, 2700);
        if (m->target >= 0 && a->enemies[m->target].alive) {
            V3 desired = vnorm(vsub(a->enemies[m->target].pos, m->pos));
            m->dir = vnorm(vadd(m->dir, vmul(vsub(desired, m->dir), fminf(1.0f, 3.2f * dt))));
        }
        m->pos = vadd(m->pos, vmul(m->dir, m->speed * dt));
        m->life -= dt;
        for (int j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &a->enemies[j];
            if (!e->alive) continue;
            V3 d = vsub(e->pos, m->pos);
            if (vdot(d, d) < 95.0f * 95) {
                e->alive = 0;
                e->respawn = 5;
                add_boom(a, e->pos);
                a->kills++;
                m->active = 0;
                break;
            }
        }
        if (m->active && m->pos.y < scene_ground(a->scene, m->pos.x, m->pos.z)) { add_boom(a, m->pos); m->active = 0; }
        if (m->life <= 0) m->active = 0;
    }
    for (int i = 0; i < MAX_BOOMS; i++)
        if (a->booms[i].active && (a->booms[i].t += dt) > 1.3f) a->booms[i].active = 0;
}

/* ------------------------------------------------------------------ vuelo */

static float dz(float v) { return fabsf(v) < 0.12f ? 0 : v; }

void update(App *a, float dt) {
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
    a->pos = vadd(a->pos, vmul(a->fwd, a->speed * dt));
    float g = scene_ground(a->scene, a->pos.x, a->pos.z) + 40;
    if (a->pos.y < g) a->pos.y = g;
    a->cam_yaw = dz(a->rx) * 3.1f;
    a->cam_pitch = dz(a->ry) * 1.2f;
    update_enemies(a, dt);
    update_combat(a, dt);
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
    if (a->target >= 0) {
        V3 p = a->enemies[a->target].pos;
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

/* cuadrado orientado a la camara (explosiones, estelas) */
static int push_billboard(float *o, int n, V3 p, V3 cr, V3 cu, float sz) {
    V3 c[4] = {vadd(vadd(p, vmul(cr, -sz)), vmul(cu, -sz)), vadd(vadd(p, vmul(cr, sz)), vmul(cu, -sz)),
               vadd(vadd(p, vmul(cr, -sz)), vmul(cu, sz)), vadd(vadd(p, vmul(cr, sz)), vmul(cu, sz))};
    int idx[6] = {0, 1, 2, 1, 3, 2};
    for (int i = 0; i < 6; i++) {
        o[(n + i) * 3] = c[idx[i]].x; o[(n + i) * 3 + 1] = c[idx[i]].y; o[(n + i) * 3 + 2] = c[idx[i]].z;
    }
    return n + 6;
}

void draw(App *a) {
    if (a->status != 1) {
        float c[3] = {0.05f, 0.05f, 0.25f};
        if (a->status == -2) { c[0] = 0.6f; c[1] = 0.0f; c[2] = 0.0f; }
        glClearColor(c[0], c[1], c[2], 1);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    const float sky[3] = {0.55f, 0.68f, 0.85f};
    glViewport(0, 0, a->w, a->h);
    glClearColor(sky[0], sky[1], sky[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* camara de persecucion cercana, orbitable con la palanca derecha */
    V3 back = vmul(a->fwd, -1);
    back = vrot(back, a->up, a->cam_yaw);
    back = vrot(back, a->right, a->cam_pitch);
    V3 eye = vadd(vadd(a->pos, vmul(back, 125)), vmul(a->up, 30));
    Mat4 view = lookat(eye, vadd(a->pos, vmul(a->fwd, 30)), a->up);
    Mat4 proj = perspective(1.05f, (float)a->w / a->h, 3.0f, 2048.0f * (VIEW_CELLS + 2));
    Mat4 vp = mmul(proj, view);

    glUseProgram(a->prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, a->vram_tex);
    glUniformMatrix4fv(a->u_mvp, 1, GL_FALSE, vp.m);
    glUniform3f(a->u_cam, eye.x, eye.y, eye.z);
    glUniform3f(a->u_fogcol, sky[0], sky[1], sky[2]);
    glUniform2f(a->u_fog, 2048.0f * (VIEW_CELLS - 6), 2048.0f * VIEW_CELLS);
    V3 light = vnorm(v3(0.4f, 0.85f, 0.3f));
    glUniform3f(a->u_light, light.x, light.y, light.z);

    /* terreno: una llamada por celda visible */
    Scene *s = a->scene;
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

    /* enemigos (aviones de la mision, con sus colores precalculados) */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &a->enemies[i];
        if (e->alive) draw_mesh(a, a->vao_enemy[e->type], s->enemy[e->type].n, basis(e->right, e->up, e->fwd, e->pos), 0);
    }
    /* misiles */
    for (int i = 0; i < MAX_MISSILES; i++) {
        Missile *m = &a->missiles[i];
        if (!m->active) continue;
        V3 r = vnorm(vcross(m->dir, WORLD_UP)), u = vcross(r, m->dir);
        draw_mesh(a, a->vao_missile, s->missile.n, basis(r, u, m->dir, m->pos), 0);
    }
    /* avion del jugador */
    draw_mesh(a, a->vao_plane, s->plane.n, basis(a->right, a->up, a->fwd, a->pos), 1);
    glBindVertexArray(0);

    /* estelas de misiles y explosiones: cuadrados orientados a la camara, aditivos */
    V3 cf = vnorm(vsub(vadd(a->pos, vmul(a->fwd, 30)), eye));
    V3 cr = vnorm(vcross(cf, a->up)), cu = vcross(cr, cf);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    int n = 0;
    for (int i = 0; i < MAX_MISSILES; i++)
        if (a->missiles[i].active) n = push_billboard(g_solid, n, vsub(a->missiles[i].pos, vmul(a->missiles[i].dir, 8)), cr, cu, 6);
    draw_solid(a, g_solid, n, &vp, 1.0f, 0.85f, 0.5f, 0.9f);
    for (int i = 0; i < MAX_BOOMS; i++) {
        Boom *b = &a->booms[i];
        if (!b->active) continue;
        float t = b->t / 1.3f;
        n = push_billboard(g_solid, 0, b->pos, cr, cu, 60 + 260 * t);
        draw_solid(a, g_solid, n, &vp, 1.0f, 0.45f, 0.1f, 0.85f * (1 - t));
        n = push_billboard(g_solid, 0, b->pos, cr, cu, 30 + 120 * t);
        draw_solid(a, g_solid, n, &vp, 1.0f, 0.9f, 0.5f, 0.9f * (1 - t));
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    draw_hud(a, &vp);
}
