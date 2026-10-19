/* Dibujo por GLES a alta resolucion (ver psx_hw.h). */
#include "psx_hw.h"

#include <GLES3/gl3.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* un vertice: posicion y uv en coordenadas de la VRAM, color 0-255, y lo comun a la primitiva
 * (pagina, CLUT, profundidad + banderas, ventana) como enteros planos */
typedef struct {
    float x, y, u, v;
    uint8_t r, g, b, a;
    int32_t t[4];
    float uvr[4];   /* rango de uv de la primitiva (min u, min v, max u, max v) */
} HwVert;

#define F_DIRECT 128   /* uv = coordenadas absolutas de la VRAM, color de 15 bits tal cual */

static const char *VS =
    "#version 300 es\n"
    "layout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv; layout(location=2) in vec3 a_col;\n"
    "layout(location=3) in ivec4 a_tex; layout(location=4) in vec4 a_uvr;\n"
    "uniform vec2 u_origin; uniform vec2 u_size; uniform vec2 u_shift;\n"
    "out vec2 v_uv; out vec3 v_col; out vec2 v_pos; flat out ivec4 v_tex; flat out vec4 v_uvr;\n"
    "void main() {\n"
    "  v_uv = a_uv; v_col = a_col; v_tex = a_tex; v_pos = a_pos; v_uvr = a_uvr;\n"
    "  gl_Position = vec4((a_pos + u_shift - u_origin) / u_size * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* la GPU evalua cada pixel en su coordenada entera (la esquina) y GL en el centro: la geometria
 * va desplazada casi medio pixel de salida (u_shift = medio pixel - u_delta), asi que la
 * cobertura se evalua un poco dentro de la esquina (los bordes izquierdo y superior cuentan y el
 * derecho y el inferior no, como en la GPU). Color, uv y tramado se llevan a la esquina exacta
 * restando u_delta pixeles de salida (si no, una uv que decrece leeria el texel anterior) */
static const char *FS =
    "#version 300 es\n"
    "precision highp float; precision highp int; precision highp usampler2D;\n"
    "uniform usampler2D u_vram; uniform int u_pass; uniform int u_dither; uniform float u_delta;\n"
    "in vec2 v_uv; in vec3 v_col; in vec2 v_pos; flat in ivec4 v_tex; flat in vec4 v_uvr;\n"
    "const float DITHER[16] = float[16](-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0, -3.0, 1.0, -4.0, 0.0, 3.0, -1.0, 2.0, -2.0);\n"
    "float dith(int flags) { if (u_dither == 0 || (flags & 16) == 0) return 0.0;\n"
    "  ivec2 p = ivec2(floor(v_pos - u_delta * (dFdx(v_pos) + dFdy(v_pos)) + 0.001)); return DITHER[(p.y & 3) * 4 + (p.x & 3)]; }\n"
    "out vec4 o;\n"
    "uint fetch(int x, int y) { return texelFetch(u_vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    "vec3 x5(uint c) { vec3 v = vec3(float(c & 31u), float((c >> 5) & 31u), float((c >> 10) & 31u));\n"
    "  return (v * 8.0 + floor(v / 4.0)) / 255.0; }\n"
    "void main() {\n"
    "  int flags = v_tex.z >> 4, depth = v_tex.z & 15;\n"
    "  vec2 uv = v_uv - u_delta * (dFdx(v_uv) + dFdy(v_uv));\n"
    "  if ((flags & 128) == 0) uv = clamp(uv, v_uvr.xy, v_uvr.zw);   /* sin salirse de la textura del poligono */\n"
    "  vec3 col = v_col - u_delta * (dFdx(v_col) + dFdy(v_col));\n"
    "  if ((flags & 128) != 0) { uint d = fetch(int(floor(uv.x + 0.001)), int(floor(uv.y + 0.001))); bool s = (d & 0x8000u) != 0u;\n"
    "    if ((u_pass == 1 && s) || (u_pass == 2 && !s)) discard; o = vec4(x5(d), 1.0); return; }\n"
    "  if ((flags & 1) == 0) {\n"
    "    vec3 c = floor(clamp(floor(col + 0.001) + dith(flags), 0.0, 255.0) / 8.0);\n"
    "    o = vec4((c * 8.0 + floor(c / 4.0)) / 255.0, 1.0); return; }\n"
    "  int u = int(floor(uv.x + 0.001)) & 255, v = int(floor(uv.y + 0.001)) & 255;\n"
    "  int tw = v_tex.w, mx = tw & 31, my = (tw >> 5) & 31, ox = (tw >> 10) & 31, oy = (tw >> 15) & 31;\n"
    "  u = (u & ~(mx * 8)) | ((ox & mx) * 8); v = (v & ~(my * 8)) | ((oy & my) * 8);\n"
    "  int tpx = v_tex.x & 0xFFFF, tpy = v_tex.x >> 16, cx = v_tex.y & 0xFFFF, cy = v_tex.y >> 16;\n"
    "  uint t;\n"
    "  if (depth == 0) { uint w = fetch(tpx + u / 4, tpy + v); t = fetch(cx + int((w >> uint((u & 3) * 4)) & 15u), cy); }\n"
    "  else if (depth == 1) { uint w = fetch(tpx + u / 2, tpy + v); t = fetch(cx + int((w >> uint((u & 1) * 8)) & 255u), cy); }\n"
    "  else t = fetch(tpx + u, tpy + v);\n"
    "  if (t == 0u) discard;\n"
    "  bool stp = (t & 0x8000u) != 0u;\n"
    "  if (u_pass == 1 && stp) discard;\n"
    "  if (u_pass == 2 && !stp) discard;\n"
    "  if ((flags & 2) != 0) { o = vec4(x5(t), 1.0); return; }\n"
    "  vec3 tc = vec3(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u)) * 8.0;\n"
    "  vec3 c = floor(clamp(floor(tc * floor(col + 0.001) / 128.0) + dith(flags), 0.0, 255.0) / 8.0);\n"
    "  o = vec4((c * 8.0 + floor(c / 4.0)) / 255.0, 1.0);\n"
    "}\n";

static GLuint g_prog, g_vao, g_vbo, g_vram, g_fbo, g_out, g_stencil;
static GLint u_origin, u_size, u_vram, u_pass, u_dither, u_shift, u_delta;
static int g_dither;
void psx_hw_dither(int on) { g_dither = on; }
static int g_ow, g_oh, g_scale, g_x0, g_y0;
static HwVert *g_buf;
static int g_n, g_cap;
static int g_sc[4] = {-1, -1, -1, -1}, g_blend = -1, g_pass = -1, g_st = -2;
/* bit de mascara (bit 15 de la VRAM): el stencil de la imagen lo lleva pixel a pixel. Solo si la
 * lista tiene alguna primitiva que lo comprueba (g_track); si no, no hace falta y no se paga */
static int g_track;

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "psx_hw: shader: %s\n", log);
    }
    return s;
}

int psx_hw_init(void) {
    g_prog = glCreateProgram();
    glAttachShader(g_prog, shader(GL_VERTEX_SHADER, VS));
    glAttachShader(g_prog, shader(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(g_prog);
    GLint ok;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "psx_hw: el programa no enlaza\n"); return -1; }
    u_origin = glGetUniformLocation(g_prog, "u_origin");
    u_size = glGetUniformLocation(g_prog, "u_size");
    u_vram = glGetUniformLocation(g_prog, "u_vram");
    u_pass = glGetUniformLocation(g_prog, "u_pass");
    u_dither = glGetUniformLocation(g_prog, "u_dither");
    u_shift = glGetUniformLocation(g_prog, "u_shift");
    u_delta = glGetUniformLocation(g_prog, "u_delta");
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HwVert), (void *)offsetof(HwVert, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(HwVert), (void *)offsetof(HwVert, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(HwVert), (void *)offsetof(HwVert, r));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, sizeof(HwVert), (void *)offsetof(HwVert, t));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(HwVert), (void *)offsetof(HwVert, uvr));
    glBindVertexArray(0);
    glGenTextures(1, &g_vram);
    glBindTexture(GL_TEXTURE_2D, g_vram);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16UI, 1024, 512);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &g_fbo);
    return 0;
}

void psx_hw_shutdown(void) {
    glDeleteProgram(g_prog);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteBuffers(1, &g_vbo);
    glDeleteTextures(1, &g_vram);
    if (g_out) glDeleteTextures(1, &g_out);
    if (g_stencil) glDeleteRenderbuffers(1, &g_stencil);
    glDeleteFramebuffers(1, &g_fbo);
    free(g_buf);
    g_buf = NULL; g_cap = g_n = 0; g_out = 0; g_stencil = 0; g_ow = g_oh = 0;
}

static void flush(void) {
    if (!g_n) return;
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_n * (GLsizeiptr)sizeof(HwVert), g_buf, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_n);
    g_n = 0;
}

/* recorte (area de dibujo, en VRAM, inclusiva; ax2 < 0: sin recorte), mezcla (-1 ninguna,
 * 0-3 = abr), pasada (0 normal, 1 texeles con el bit 15 a 0, 2 con el bit 15 a 1) y mascara
 * (st: -1 sin stencil; si no, bit 0 = valor del bit 15 que se escribe, bit 1 = comprobar: solo se
 * dibuja donde el bit 15 esta a 0) */
static void state(int ax1, int ay1, int ax2, int ay2, int blend, int pass, int st) {
    int sc[4];
    if (ax2 < 0) { sc[0] = 0; sc[1] = 0; sc[2] = g_ow; sc[3] = g_oh; }
    else {
        int x1 = (ax1 - g_x0) * g_scale, y1 = (ay1 - g_y0) * g_scale;
        int x2 = (ax2 + 1 - g_x0) * g_scale, y2 = (ay2 + 1 - g_y0) * g_scale;
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > g_ow) x2 = g_ow;
        if (y2 > g_oh) y2 = g_oh;
        sc[0] = x1; sc[1] = y1; sc[2] = x2 > x1 ? x2 - x1 : 0; sc[3] = y2 > y1 ? y2 - y1 : 0;
    }
    if (!memcmp(sc, g_sc, sizeof sc) && blend == g_blend && pass == g_pass && st == g_st) return;
    flush();
    if (st != g_st) {
        if (st < 0) glDisable(GL_STENCIL_TEST);
        else {
            int val = st & 1, chk = st >> 1;
            glEnable(GL_STENCIL_TEST);
            glStencilMask(1);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            /* comprobando: pasa donde el stencil es 0 (con 1 a escribir, "1 > s"; con 0, "0 == s") */
            if (chk) glStencilFunc(val ? GL_GREATER : GL_EQUAL, val, 1);
            else glStencilFunc(GL_ALWAYS, val, 1);
        }
        g_st = st;
    }
    if (memcmp(sc, g_sc, sizeof sc)) { glScissor(sc[0], sc[1], sc[2], sc[3]); memcpy(g_sc, sc, sizeof sc); }
    if (blend != g_blend) {
        if (blend < 0) glDisable(GL_BLEND);
        else {
            glEnable(GL_BLEND);
            switch (blend) {
            case 0: glBlendColor(0, 0, 0, 0.5f); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA); break;
            case 1: glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE); break;
            case 2: glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); glBlendFunc(GL_ONE, GL_ONE); break;
            default: glBlendColor(0, 0, 0, 0.25f); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE); break;
            }
        }
        g_blend = blend;
    }
    if (pass != g_pass) { glUniform1i(u_pass, pass); g_pass = pass; }
}

static HwVert *push(int k) {
    if (g_n + k > g_cap) {
        g_cap = (g_n + k) * 2 + 1024;
        g_buf = realloc(g_buf, (size_t)g_cap * sizeof *g_buf);
    }
    HwVert *v = &g_buf[g_n];
    g_n += k;
    return v;
}

static void tex_of(const PsxHwPrim *p, int flags, int32_t t[4]) {
    t[0] = p->tp_x | p->tp_y << 16;
    t[1] = p->clut_x | p->clut_y << 16;
    t[2] = (p->depth & 15) | flags << 4;
    t[3] = p->tw_mx | p->tw_my << 5 | p->tw_ox << 10 | p->tw_oy << 15;
}

static void vert(HwVert *v, float x, float y, float u, float vv, int r, int g, int b, const int32_t t[4]) {
    v->x = x; v->y = y; v->u = u; v->v = vv;
    v->r = (uint8_t)r; v->g = (uint8_t)g; v->b = (uint8_t)b; v->a = 255;
    memcpy(v->t, t, sizeof v->t);
}

static void quad(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, int r, int g, int b,
                 const int32_t t[4]) {
    HwVert *v = push(6);
    vert(&v[0], x1, y1, u1, v1, r, g, b, t);
    vert(&v[1], x2, y1, u2, v1, r, g, b, t);
    vert(&v[2], x1, y2, u1, v2, r, g, b, t);
    vert(&v[3], x2, y1, u2, v1, r, g, b, t);
    vert(&v[4], x2, y2, u2, v2, r, g, b, t);
    vert(&v[5], x1, y2, u1, v2, r, g, b, t);
}

/* rango de uv de los k ultimos vertices (los de la primitiva recien emitida) */
static void set_uvr(int k) {
    HwVert *v = &g_buf[g_n - k];
    float r[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    for (int i = 0; i < k; i++) {
        if (v[i].u < r[0]) r[0] = v[i].u;
        if (v[i].v < r[1]) r[1] = v[i].v;
        if (v[i].u > r[2]) r[2] = v[i].u;
        if (v[i].v > r[3]) r[3] = v[i].v;
    }
    for (int i = 0; i < k; i++) memcpy(v[i].uvr, r, sizeof r);
}

static void upload(const uint16_t *v) {
    glBindTexture(GL_TEXTURE_2D, g_vram);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512, GL_RED_INTEGER, GL_UNSIGNED_SHORT, v);
}

/* una primitiva en las pasadas que necesite */
static const PsxHwList *g_list;

static void draw_prim(const PsxHwPrim *p) {
    if (p->kind == PSX_HW_UPLOAD) {   /* la VRAM del software cambio fuera de la imagen: a la textura */
        flush();
        glBindTexture(GL_TEXTURE_2D, g_vram);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        glTexSubImage2D(GL_TEXTURE_2D, 0, p->x[0], p->y[0], p->x[1], p->y[1], GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                        g_list->data + p->data_off);
        return;
    }
    int f = p->flags, semi = (f & PSX_HWF_SEMI) != 0, tex = (f & PSX_HWF_TEX) != 0;
    int mset = (f & PSX_HWF_MASKSET) != 0, mchk = (f & PSX_HWF_MASKCHK) != 0;
    int32_t t[4];
    int passes[2], np = 0;
    /* bit 15 escrito (como la GPU por software): texel con textura (copias: el de origen) | poner
     * mascara (las copias y los rellenos no la usan). Con stencil, las texturas van en dos pasadas
     * para escribir cada valor */
    int vram = p->kind == PSX_HW_VRAM, fill = p->kind == PSX_HW_FILL;
    if ((semi && tex) || (g_track && (vram || (tex && !mset)))) { passes[np++] = 1; passes[np++] = 2; }
    else passes[np++] = 0;
    for (int k = 0; k < np; k++) {
        int pass = passes[k], blend = semi && (!tex || pass == 2) ? p->abr : -1;
        int st = -1;
        if (g_track) {
            if (fill) st = 0;
            else if (vram) st = pass == 2;
            else st = (mset || pass == 2) | (mchk ? 2 : 0);
        }
        if (fill || (vram && p->ax2 < 0)) state(0, 0, -1, -1, -1, 0, st);
        else state(p->ax1, p->ay1, p->ax2, p->ay2, blend, pass, st);
        switch (p->kind) {
        case PSX_HW_TRI: {
            tex_of(p, (tex ? 1 : 0) | (f & PSX_HWF_RAW ? 2 : 0) | (f & PSX_HWF_DITHER ? 16 : 0), t);
            HwVert *v = push(3);
            for (int i = 0; i < 3; i++) {
                int r = p->r[i], g = p->g[i], b = p->b[i];
                if (!(f & PSX_HWF_GOURAUD)) { r = p->r[0]; g = p->g[0]; b = p->b[0]; }
                vert(&v[i], p->x[i], p->y[i], p->u[i], p->v[i], r, g, b, t);
            }
            set_uvr(3);
            break;
        }
        case PSX_HW_RECT:
            tex_of(p, (tex ? 1 : 0) | (f & PSX_HWF_RAW ? 2 : 0), t);
            quad(p->x[0], p->y[0], p->x[0] + p->x[1], p->y[0] + p->y[1], p->u[0], p->v[0], p->u[0] + p->x[1],
                 p->v[0] + p->y[1], p->r[0], p->g[0], p->b[0], t);
            set_uvr(6);
            for (int i = 0; i < 6; i++) { g_buf[g_n - 6 + i].uvr[2] -= 0.001f; g_buf[g_n - 6 + i].uvr[3] -= 0.001f; }
            break;
        case PSX_HW_FILL:
            tex_of(p, 0, t);
            quad(p->x[0], p->y[0], p->x[0] + p->x[1], p->y[0] + p->y[1], 0, 0, 0, 0, p->r[0], p->g[0], p->b[0], t);
            break;
        case PSX_HW_VRAM:   /* copia o carga a la imagen: desde (clut_x, clut_y) de la textura */
            tex_of(p, F_DIRECT, t);
            quad(p->x[0], p->y[0], p->x[0] + p->x[1], p->y[0] + p->y[1], p->clut_x, p->clut_y, p->clut_x + p->x[1],
                 p->clut_y + p->y[1], 0, 0, 0, t);
            break;
        case PSX_HW_LINE: {
            /* la GPU recorre el eje mayor pixel a pixel (los dos extremos incluidos) y en el menor
             * redondea a + d * i / pasos, con los empates lejos de cero. Aqui: un paralelogramo de un
             * pixel de grueso cuyo borde sigue esa recta (pendiente d menor / d mayor), alargado un
             * pixel en el eje mayor para cubrir el ultimo, y desplazado en el menor para redondear
             * igual que la GPU (con el muestreo en la esquina de u_shift) */
            tex_of(p, f & PSX_HWF_DITHER ? 16 : 0, t);
            float ax = p->x[0], ay = p->y[0], bx = p->x[1], by = p->y[1];
            int gr = (f & PSX_HWF_GOURAUD) != 0;
            int r1 = gr ? p->r[1] : p->r[0], g1 = gr ? p->g[1] : p->g[0], b1 = gr ? p->b[1] : p->b[0];
            float dx = bx - ax, dy = by - ay;
            int xm = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
            /* en coordenadas (mayor, menor) */
            float ma = xm ? ax : ay, mb = xm ? bx : by, na = xm ? ay : ax, dm = mb - ma, dn = xm ? dy : dx;
            float k = dm != 0 ? dn / dm : 0;
            float lo = ma < mb ? ma : mb, hi = (ma < mb ? mb : ma) + 1;
            /* la cobertura se mira en (menor + d, mayor + d), d = u_delta en pixeles de la VRAM: el
             * pixel del menor m cuenta si m >= n(mayor) + off + (k - 1) d. Se quiere m >= n - 0.5 + e
             * (empate hacia arriba, como la GPU con d menor > 0) o n - 0.5 - e (hacia abajo con
             * d menor < 0: lejos de cero) */
            float dl = (1.0f / 128.0f) / (float)g_scale, e = 0.01f;
            float off = (dn > 0 ? -0.5f + e : -0.5f - e) - (k - 1) * dl;
            float n1 = na + k * (lo - ma) + off, n2 = na + k * (hi - ma) + off;
            /* color en cada extremo del paralelogramo (el de a en el lado de a) */
            int ra = p->r[0], ga = p->g[0], ba = p->b[0];
            int c1r = ma <= mb ? ra : r1, c1g = ma <= mb ? ga : g1, c1b = ma <= mb ? ba : b1;
            int c2r = ma <= mb ? r1 : ra, c2g = ma <= mb ? g1 : ga, c2b = ma <= mb ? b1 : ba;
            /* el paralelogramo llega un pixel mas alla del ultimo: ahi el color sigue la misma recta
             * (la GPU va de a a b en `pasos`) */
            int steps = (int)(dm < 0 ? -dm : dm);
            if (gr && steps) {
#define EXT(c2, c1) c2 = (int)lrintf((float)c2 + (float)(c2 - c1) / (float)steps), c2 = c2 < 0 ? 0 : c2 > 255 ? 255 : c2
                EXT(c2r, c1r); EXT(c2g, c1g); EXT(c2b, c1b);
#undef EXT
            }
            HwVert *v = push(6);
#define LV(i, m, n, R, G, B) vert(&v[i], xm ? (m) : (n), xm ? (n) : (m), 0, 0, R, G, B, t)
            LV(0, lo, n1, c1r, c1g, c1b);
            LV(1, hi, n2, c2r, c2g, c2b);
            LV(2, lo, n1 + 1, c1r, c1g, c1b);
            LV(3, hi, n2, c2r, c2g, c2b);
            LV(4, hi, n2 + 1, c2r, c2g, c2b);
            LV(5, lo, n1 + 1, c1r, c1g, c1b);
#undef LV
            break;
        }
        }
    }
}

void psx_hw_frame(const PsxHwList *l, const uint16_t *bg, int x0, int y0, int w, int h, int scale) {
    g_list = l;
    int ow = w * scale, oh = h * scale;
    if (ow != g_ow || oh != g_oh) {
        if (g_out) glDeleteTextures(1, &g_out);
        glGenTextures(1, &g_out);
        glBindTexture(GL_TEXTURE_2D, g_out);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, ow, oh);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!g_stencil) glGenRenderbuffers(1, &g_stencil);
        glBindRenderbuffer(GL_RENDERBUFFER, g_stencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, ow, oh);
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_out, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_stencil);
        g_ow = ow; g_oh = oh;
    }
    g_scale = scale; g_x0 = x0; g_y0 = y0;
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, ow, oh);
    glUseProgram(g_prog);
    glUniform2f(u_origin, (float)x0, (float)y0);
    glUniform2f(u_size, (float)w, (float)h);
    glUniform1i(u_dither, g_dither);
    const float delta = 1.0f / 128.0f;   /* pixeles de salida */
    float sh = (0.5f - delta) / (float)scale;   /* en pixeles de la VRAM: casi medio pixel de salida */
    glUniform2f(u_shift, sh, sh);
    glUniform1f(u_delta, delta);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(u_vram, 0);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnable(GL_SCISSOR_TEST);
    g_sc[0] = g_sc[1] = g_sc[2] = g_sc[3] = -1;
    g_blend = -2; g_pass = -1; g_st = -2;
    g_track = 0;
    for (int i = 0; i < l->n && !g_track; i++) g_track = (l->p[i].flags & PSX_HWF_MASKCHK) && l->p[i].kind != PSX_HW_UPLOAD;
    static int nomask = -1;   /* prueba del comparador (caso positivo conocido): sin bit de mascara */
    if (nomask < 0) nomask = getenv("PSX_HW_NOMASK") != NULL;
    if (nomask) g_track = 0;
    /* fondo y texturas de partida: la VRAM de antes de la lista (con su bit 15 en el stencil) */
    upload(bg);
    int32_t t[4] = {0, 0, F_DIRECT << 4, 0};
    for (int pass = g_track ? 1 : 0; pass <= (g_track ? 2 : 0); pass++) {
        state(0, 0, -1, -1, -1, pass, g_track ? pass == 2 : -1);
        quad((float)x0, (float)y0, (float)(x0 + w), (float)(y0 + h), (float)x0, (float)y0, (float)(x0 + w), (float)(y0 + h), 0, 0, 0, t);
    }
    flush();
    /* primitivas; las UPLOAD actualizan la textura en su sitio */
    for (int i = 0; i < l->n; i++) draw_prim(&l->p[i]);
    flush();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned psx_hw_texture(int *w, int *h) {
    if (w) *w = g_ow;
    if (h) *h = g_oh;
    return g_out;
}

void psx_hw_read(uint32_t *out) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glReadPixels(0, 0, g_ow, g_oh, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
