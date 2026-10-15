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
    glGenTextures(1, &a->vram_tex);
    glBindTexture(GL_TEXTURE_2D, a->vram_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, VRAM_W, VRAM_H, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    setup_vao(&a->vao_tiles, &a->vbo_tiles);
    setup_vao(&a->vao_plane, &a->vbo_plane);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);  /* el PS1 descarta con NCLIP; aqui se dibujan las dos caras */
}

void gl_upload(App *a) {
    Scene *s = a->scene;
    glBindTexture(GL_TEXTURE_2D, a->vram_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VRAM_W, VRAM_H, GL_RED_INTEGER, GL_UNSIGNED_SHORT, s->vram);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo_tiles);
    glBufferData(GL_ARRAY_BUFFER, s->tiles.n * sizeof(Vtx), s->tiles.v, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, a->vbo_plane);
    glBufferData(GL_ARRAY_BUFFER, s->plane.n * sizeof(Vtx), s->plane.v, GL_STATIC_DRAW);
    a->loaded_gl = 1;
}

void reset_flight(App *a) {
    a->pos = v3(1024, 0, -1024);
    a->pos.y = scene_ground(a->scene, a->pos.x, a->pos.z) + 2500;
    a->right = v3(1, 0, 0);
    a->up = v3(0, 1, 0);
    a->fwd = v3(0, 0, -1);
    a->speed = 450;
    a->cam_yaw = a->cam_pitch = 0;
}

/* ------------------------------------------------------------------ vuelo */

static float dz(float v) { return fabsf(v) < 0.12f ? 0 : v; }

void update(App *a, float dt) {
    float pitch = dz(a->ly), roll = dz(a->lx), yaw = (float)(a->r1 - a->l1);
    /* cabeceo: palanca hacia delante = morro abajo (como un avion) */
    float pr = pitch * 1.6f * dt, rr = roll * 2.8f * dt, yr = yaw * 0.6f * dt;
    a->up = vrot(a->up, a->right, -pr);
    a->fwd = vrot(a->fwd, a->right, -pr);
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
}

/* ------------------------------------------------------------------ dibujo */

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

    /* camara de persecucion, orbitable con la palanca derecha */
    V3 back = vmul(a->fwd, -1);
    back = vrot(back, a->up, a->cam_yaw);
    back = vrot(back, a->right, a->cam_pitch);
    V3 eye = vadd(vadd(a->pos, vmul(back, 260)), vmul(a->up, 70));
    Mat4 view = lookat(eye, vadd(a->pos, vmul(a->fwd, 40)), a->up);
    Mat4 proj = perspective(1.05f, (float)a->w / a->h, 8.0f, 2048.0f * (VIEW_CELLS + 2));
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

    /* avion: base (derecha, arriba, -adelante) porque el morro del modelo mira a -z */
    Mat4 pm = {{a->right.x, a->right.y, a->right.z, 0, a->up.x, a->up.y, a->up.z, 0,
                -a->fwd.x, -a->fwd.y, -a->fwd.z, 0, a->pos.x, a->pos.y, a->pos.z, 1}};
    glUniformMatrix4fv(a->u_model, 1, GL_FALSE, pm.m);
    glUniform1i(a->u_lit, 1);
    glBindVertexArray(a->vao_plane);
    glDrawArrays(GL_TRIANGLES, 0, s->plane.n);
    glBindVertexArray(0);
}

