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
    "layout(location = 0) out vec4 o;\n"
    "layout(location = 1) out vec4 m;   /* bit 15 que deja el pixel (alfa), para las imagenes conservadas */\n"
    "/* salida con la semitransparencia de la primitiva (abr) resuelta por el alfa: la mezcla es\n"
    " * siempre ONE, ONE_MINUS_SRC_ALPHA (opaco: alfa 1; abr 0: F/2 + B/2; 1: F + B; 3: F/4 + B);\n"
    " * abr 2 (B - F) va con su propia mezcla (resta) */\n"
    "void put(vec3 c, bool semi, int abr) {\n"
    "  if (!semi || abr == 2) o = vec4(c, 1.0); else if (abr == 0) o = vec4(c * 0.5, 0.5);\n"
    "  else if (abr == 1) o = vec4(c, 0.0); else o = vec4(c * 0.25, 0.0); }\n"
    "uint fetch(int x, int y) { return texelFetch(u_vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    "vec3 x5(uint c) { vec3 v = vec3(float(c & 31u), float((c >> 5) & 31u), float((c >> 10) & 31u));\n"
    "  return (v * 8.0 + floor(v / 4.0)) / 255.0; }\n"
    "void main() {\n"
    "  int flags = (v_tex.z >> 4) & 255, depth = v_tex.z & 15, abr = (v_tex.z >> 12) & 3; bool semi = (flags & 4) != 0;\n"
    "  bool mset = (flags & 32) != 0; m = vec4(0.0, 0.0, 0.0, mset ? 1.0 : 0.0);\n"
    "  vec2 uv = v_uv - u_delta * (dFdx(v_uv) + dFdy(v_uv));\n"
    "  if ((flags & 128) == 0) uv = clamp(uv, v_uvr.xy, v_uvr.zw);   /* sin salirse de la textura del poligono */\n"
    "  vec3 col = v_col - u_delta * (dFdx(v_col) + dFdy(v_col));\n"
    "  if ((flags & 128) != 0) { uint d = fetch(int(floor(uv.x + 0.001)), int(floor(uv.y + 0.001))); bool s = (d & 0x8000u) != 0u;\n"
    "    if ((u_pass == 1 && s) || (u_pass == 2 && !s)) discard; o = vec4(x5(d), 1.0); m.a = s || mset ? 1.0 : 0.0; return; }\n"
    "  if ((flags & 1) == 0) {\n"
    "    vec3 c = floor(clamp(floor(col + 0.001) + dith(flags), 0.0, 255.0) / 8.0);\n"
    "    put((c * 8.0 + floor(c / 4.0)) / 255.0, semi, abr); return; }\n"
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
    "  m.a = stp || mset ? 1.0 : 0.0;\n"
    "  if (u_pass == 1 && stp) discard;\n"
    "  if (u_pass == 2 && !stp) discard;\n"
    "  if ((flags & 2) != 0) { put(x5(t), semi && stp, abr); return; }\n"
    "  vec3 tc = vec3(float(t & 31u), float((t >> 5) & 31u), float((t >> 10) & 31u)) * 8.0;\n"
    "  vec3 c = floor(clamp(floor(tc * floor(col + 0.001) / 128.0) + dith(flags), 0.0, 255.0) / 8.0);\n"
    "  put((c * 8.0 + floor(c / 4.0)) / 255.0, semi && stp, abr);\n"
    "}\n";

static GLuint g_prog, g_vao, g_vbo, g_vram, g_fbo, g_out, g_stencil;
/* etapa 8, paso 2: cada imagen de la VRAM (doble bufer) se conserva en la GPU, color y bit de
 * mascara (stencil), como en una GPU de verdad: las reales se dibujan encima de la suya y las
 * intermedias parten de una copia */
#define NTARGET 4
typedef struct { int x0, y0, w, h, scale, valid; unsigned use; GLuint tex, mask, stencil, fbo; } Target;
static Target g_tg[NTARGET];
static GLuint g_cprog, g_cvao, g_cvbo, g_vfbo;   /* copia de una imagen conservada a la textura de la VRAM */
static GLuint g_sprog;                           /* stencil (bit 15) desde el segundo bufer de una imagen conservada */
static unsigned g_tg_use;
static GLuint g_last_tex, g_read_fbo;   /* lo que devuelven psx_hw_texture y psx_hw_read */
/* la VRAM como textura: tres que se turnan (la que se rellena no la esta leyendo la GPU para una
 * imagen anterior) y, de cada una, una copia de lo que tiene, para subir solo las franjas de 16
 * filas que cambian */
#define NVTEX 3
static GLuint g_vtex[NVTEX];
static uint16_t *g_shadow[NVTEX];
static int g_ring;
static GLint u_origin, u_size, u_vram, u_pass, u_dither, u_shift, u_delta;
static int g_dither;
void psx_hw_dither(int on) { g_dither = on; }
static int g_ow, g_oh, g_scale, g_x0, g_y0;
static HwVert *g_buf;
static int g_n, g_cap;
/* zona que alguna primitiva comprueba con la mascara (VRAM, inclusiva) y la ultima que lo hace:
 * solo las primitivas anteriores que la tocan necesitan el bit 15 exacto en el stencil */
static int g_cx1, g_cy1, g_cx2, g_cy2, g_clast;
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
    glGenTextures(NVTEX, g_vtex);
    for (int i = 0; i < NVTEX; i++) {
        glBindTexture(GL_TEXTURE_2D, g_vtex[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16UI, 1024, 512);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        free(g_shadow[i]);
        g_shadow[i] = NULL;   /* sin copia: la primera vez se sube entera */
    }
    g_vram = g_vtex[0];
    glGenFramebuffers(1, &g_fbo);
    return 0;
}

void psx_hw_shutdown(void) {
    glDeleteProgram(g_prog);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteBuffers(1, &g_vbo);
    glDeleteTextures(NVTEX, g_vtex);
    for (int i = 0; i < NVTEX; i++) { free(g_shadow[i]); g_shadow[i] = NULL; }
    if (g_out) glDeleteTextures(1, &g_out);
    if (g_stencil) glDeleteRenderbuffers(1, &g_stencil);
    glDeleteFramebuffers(1, &g_fbo);
    for (int i = 0; i < NTARGET; i++)
        if (g_tg[i].fbo) {
            glDeleteFramebuffers(1, &g_tg[i].fbo); glDeleteTextures(1, &g_tg[i].tex); glDeleteTextures(1, &g_tg[i].mask);
            glDeleteRenderbuffers(1, &g_tg[i].stencil);
        }
    memset(g_tg, 0, sizeof g_tg);
    if (g_cprog) { glDeleteProgram(g_cprog); glDeleteVertexArrays(1, &g_cvao); glDeleteBuffers(1, &g_cvbo); glDeleteFramebuffers(1, &g_vfbo); }
    g_cprog = 0;
    free(g_buf);
    g_buf = NULL; g_cap = g_n = 0; g_out = 0; g_stencil = 0; g_ow = g_oh = 0;
}

int psx_hw_stat_draws, psx_hw_stat_uploads, psx_hw_stat_verts;   /* medicion: llamadas por imagen */

/* lotes: los vertices de un tramo (entre dos subidas de VRAM) se suben de una vez y las
 * llamadas de dibujo se encolan con su estado; asi una imagen son unas pocas decenas de llamadas
 * en vez de una o dos por primitiva (en la Retroid, cada llamada cuesta) */
typedef struct { int first, sc[4], cls, pass, st; } Cmd;
static Cmd *g_cmd;
static int g_ncmd, g_capcmd;
/* estado aplicado a GL: recorte, mezcla (0 la comun, 1 resta), pasada, stencil */
static int g_asc[4], g_acls, g_apass, g_ast;

static void apply(const Cmd *c) {
    if (c->st != g_ast) {
        if (c->st < 0) glDisable(GL_STENCIL_TEST);
        else {
            int val = c->st & 1, chk = c->st >> 1;
            glEnable(GL_STENCIL_TEST);
            glStencilMask(1);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            /* comprobando: pasa donde el stencil es 0 (con 1 a escribir, "1 > s"; con 0, "0 == s") */
            if (chk) glStencilFunc(val ? GL_GREATER : GL_EQUAL, val, 1);
            else glStencilFunc(GL_ALWAYS, val, 1);
        }
        g_ast = c->st;
    }
    if (memcmp(c->sc, g_asc, sizeof g_asc)) { glScissor(c->sc[0], c->sc[1], c->sc[2], c->sc[3]); memcpy(g_asc, c->sc, sizeof g_asc); }
    if (c->cls != g_acls) {
        /* el alfa se reemplaza siempre: en las imagenes conservadas, el del segundo bufer es el bit 15 */
        if (c->cls == 1) { glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD); glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO); }
        else { glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD); glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO); }
        g_acls = c->cls;
    }
    if (c->pass != g_apass) { glUniform1i(u_pass, c->pass); g_apass = c->pass; }
}

/* dibuja lo encolado */
static void flush(void) {
    if (g_n) {
        psx_hw_stat_verts += g_n;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_n * (GLsizeiptr)sizeof(HwVert), g_buf, GL_STREAM_DRAW);
        for (int i = 0; i < g_ncmd; i++) {
            int end = i + 1 < g_ncmd ? g_cmd[i + 1].first : g_n;
            if (end <= g_cmd[i].first) continue;
            apply(&g_cmd[i]);
            glDrawArrays(GL_TRIANGLES, g_cmd[i].first, end - g_cmd[i].first);
            psx_hw_stat_draws++;
        }
    }
    g_n = 0;
    g_ncmd = 0;
}

/* estado de lo que se encola a continuacion: recorte (area de dibujo, en VRAM, inclusiva; ax2 < 0:
 * sin recorte), mezcla (cls: 0 la comun, 1 resta), pasada (0 normal, 1 texeles con el bit 15 a
 * 0, 2 con el bit 15 a 1) y mascara (st: -1 sin stencil; si no, bit 0 = valor del bit 15 que se
 * escribe, bit 1 = comprobar: solo se dibuja donde el bit 15 esta a 0) */
static void state(int ax1, int ay1, int ax2, int ay2, int cls, int pass, int st) {
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
    if (g_ncmd > 0) {
        Cmd *c = &g_cmd[g_ncmd - 1];
        if (!memcmp(sc, c->sc, sizeof sc) && cls == c->cls && pass == c->pass && st == c->st) return;
        if (c->first == g_n) { memcpy(c->sc, sc, sizeof sc); c->cls = cls; c->pass = pass; c->st = st; return; }
    }
    if (g_ncmd == g_capcmd) {
        g_capcmd = g_capcmd * 2 + 256;
        g_cmd = realloc(g_cmd, (size_t)g_capcmd * sizeof *g_cmd);
    }
    Cmd *c = &g_cmd[g_ncmd++];
    c->first = g_n;
    memcpy(c->sc, sc, sizeof sc);
    c->cls = cls; c->pass = pass; c->st = st;
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
    if (p->flags & PSX_HWF_MASKSET) flags |= 32;   /* la primitiva pone el bit 15 */
    t[2] = (p->depth & 15) | flags << 4 | (p->abr & 3) << 12;
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

/* la VRAM de partida a la siguiente textura del turno: solo las franjas que difieren de lo que ya
 * tiene */
static void upload(const uint16_t *v) {
    g_ring = (g_ring + 1) % NVTEX;
    g_vram = g_vtex[g_ring];
    glBindTexture(GL_TEXTURE_2D, g_vram);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    uint16_t *sh = g_shadow[g_ring];
    if (!sh) {
        sh = g_shadow[g_ring] = malloc(1024 * 512 * 2);
        if (sh) memcpy(sh, v, 1024 * 512 * 2);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512, GL_RED_INTEGER, GL_UNSIGNED_SHORT, v);
        psx_hw_stat_uploads++;
        return;
    }
    for (int y = 0; y < 512;) {   /* franjas de 16 filas; las seguidas que cambian, de una vez */
        if (!memcmp(sh + y * 1024, v + y * 1024, 16 * 1024 * 2)) { y += 16; continue; }
        int y2 = y + 16;
        while (y2 < 512 && memcmp(sh + y2 * 1024, v + y2 * 1024, 16 * 1024 * 2)) y2 += 16;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, 1024, y2 - y, GL_RED_INTEGER, GL_UNSIGNED_SHORT, v + y * 1024);
        memcpy(sh + y * 1024, v + y * 1024, (size_t)(y2 - y) * 1024 * 2);
        psx_hw_stat_uploads++;
        y = y2;
    }
}

/* una primitiva en las pasadas que necesite */
static const PsxHwList *g_list;

/* caja de la primitiva en la VRAM (inclusiva) */
static void bbox(const PsxHwPrim *p, int *x1, int *y1, int *x2, int *y2) {
    if (p->kind == PSX_HW_TRI || p->kind == PSX_HW_LINE) {
        int n = p->kind == PSX_HW_TRI ? 3 : 2;
        *x1 = *x2 = p->x[0]; *y1 = *y2 = p->y[0];
        for (int i = 1; i < n; i++) {
            if (p->x[i] < *x1) *x1 = p->x[i];
            if (p->x[i] > *x2) *x2 = p->x[i];
            if (p->y[i] < *y1) *y1 = p->y[i];
            if (p->y[i] > *y2) *y2 = p->y[i];
        }
        *x2 += 1; *y2 += 1;
    } else { *x1 = p->x[0]; *y1 = p->y[0]; *x2 = p->x[0] + p->x[1]; *y2 = p->y[0] + p->y[1]; }
}

static void draw_prim(const PsxHwPrim *p, int index) {
    if (p->kind == PSX_HW_UPLOAD) {   /* la VRAM del software cambio fuera de la imagen: a la textura */
        flush();
        glBindTexture(GL_TEXTURE_2D, g_vram);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        glTexSubImage2D(GL_TEXTURE_2D, 0, p->x[0], p->y[0], p->x[1], p->y[1], GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                        g_list->data + p->data_off);
        psx_hw_stat_uploads++;
        uint16_t *sh = g_shadow[g_ring];   /* la copia sigue a la textura */
        if (sh && p->x[0] >= 0 && p->y[0] >= 0 && p->x[0] + p->x[1] <= 1024 && p->y[0] + p->y[1] <= 512)
            for (int y = 0; y < p->y[1]; y++)
                memcpy(sh + (p->y[0] + y) * 1024 + p->x[0], g_list->data + p->data_off + y * p->x[1], (size_t)p->x[1] * 2);
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
    /* el bit 15 exacto (dos pasadas en las texturas) solo antes de la ultima primitiva que comprueba
     * la mascara y si toca su zona */
    int exact = 0;
    if (g_track && index <= g_clast && (tex || vram)) {
        int x1, y1, x2, y2;
        bbox(p, &x1, &y1, &x2, &y2);
        exact = x2 >= g_cx1 && x1 <= g_cx2 && y2 >= g_cy1 && y1 <= g_cy2;
    }
    /* la semitransparencia va en el shader (una pasada), salvo la resta (abr 2) con textura: sus
     * texeles opacos con la mezcla comun y los semitransparentes con la resta */
    int sub = semi && p->abr == 2;
    if ((sub && tex) || (exact && (vram || !mset))) { passes[np++] = 1; passes[np++] = 2; }
    else passes[np++] = 0;
    for (int k = 0; k < np; k++) {
        int pass = passes[k], cls = sub && (!tex || pass == 2) ? 1 : 0;
        int st = -1;
        if (g_track) {
            if (fill) st = 0;
            else if (vram) st = pass == 2;
            else st = (mset || pass == 2) | (mchk ? 2 : 0);
        }
        if (fill || (vram && p->ax2 < 0)) state(0, 0, -1, -1, 0, 0, st);
        else state(p->ax1, p->ay1, p->ax2, p->ay2, cls, pass, st);
        switch (p->kind) {
        case PSX_HW_TRI: {
            tex_of(p, (tex ? 1 : 0) | (f & PSX_HWF_RAW ? 2 : 0) | (semi ? 4 : 0) | (f & PSX_HWF_DITHER ? 16 : 0), t);
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
            tex_of(p, (tex ? 1 : 0) | (f & PSX_HWF_RAW ? 2 : 0) | (semi ? 4 : 0), t);
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
            tex_of(p, (semi ? 4 : 0) | (f & PSX_HWF_DITHER ? 16 : 0), t);
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

static Target *target(int x0, int y0, int w, int h, int scale) {
    Target *t = NULL;
    for (int i = 0; i < NTARGET; i++)
        if (g_tg[i].fbo && g_tg[i].x0 == x0 && g_tg[i].y0 == y0 && g_tg[i].w == w && g_tg[i].h == h && g_tg[i].scale == scale) t = &g_tg[i];
    if (!t) {   /* uno nuevo, o el menos usado */
        t = &g_tg[0];
        for (int i = 1; i < NTARGET; i++) if (g_tg[i].use < t->use) t = &g_tg[i];
        if (!t->fbo) { glGenFramebuffers(1, &t->fbo); glGenTextures(1, &t->tex); glGenTextures(1, &t->mask); glGenRenderbuffers(1, &t->stencil); }
        else { glDeleteTextures(1, &t->tex); glGenTextures(1, &t->tex); glDeleteTextures(1, &t->mask); glGenTextures(1, &t->mask); }
        glBindTexture(GL_TEXTURE_2D, t->mask);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w * scale, h * scale);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, t->tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w * scale, h * scale);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, t->stencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, w * scale, h * scale);
        glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, t->mask, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t->stencil);
        static const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, bufs);
        t->x0 = x0; t->y0 = y0; t->w = w; t->h = h; t->scale = scale; t->valid = 0;
    }
    t->use = ++g_tg_use;
    return t;
}

/* etapa 8, paso 2: una imagen conservada, a la textura de la VRAM (15 bits, bit de mascara a 0),
 * para las listas que la leen como textura (efectos con la imagen anterior). Solo en la GPU */
static const char *CVS =
    "#version 300 es\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos.x / 512.0 - 1.0, a_pos.y / 256.0 - 1.0, 0.0, 1.0); }\n";
static const char *CFS =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D u_src, u_msk;\n"
    "uniform ivec2 u_org;\n"
    "uniform int u_scale;\n"
    "out uvec4 o;\n"
    "void main() {\n"
    "  ivec2 p = (ivec2(gl_FragCoord.xy) - u_org) * u_scale;\n"
    "  uvec3 c = uvec3(texelFetch(u_src, p, 0).rgb * 255.0 + 0.5) >> 3u;\n"
    "  uint s = texelFetch(u_msk, p, 0).a > 0.5 ? 0x8000u : 0u;\n"
    "  o = uvec4(c.r | (c.g << 5u) | (c.b << 10u) | s, 0u, 0u, 0u);\n"
    "}\n";
static GLint u_csrc, u_cmsk, u_corg, u_cscale;
static GLuint shader(GLenum type, const char *src);
static void copy_init(void) {
    g_cprog = glCreateProgram();
    glAttachShader(g_cprog, shader(GL_VERTEX_SHADER, CVS));
    glAttachShader(g_cprog, shader(GL_FRAGMENT_SHADER, CFS));
    glLinkProgram(g_cprog);
    u_csrc = glGetUniformLocation(g_cprog, "u_src");
    u_cmsk = glGetUniformLocation(g_cprog, "u_msk");
    u_corg = glGetUniformLocation(g_cprog, "u_org");
    u_cscale = glGetUniformLocation(g_cprog, "u_scale");
    glGenVertexArrays(1, &g_cvao);
    glGenBuffers(1, &g_cvbo);
    glBindVertexArray(g_cvao);
    glBindBuffer(GL_ARRAY_BUFFER, g_cvbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glBindVertexArray(0);
    glGenFramebuffers(1, &g_vfbo);
}
/* el stencil de la imagen que se dibuja, desde el bit 15 exacto del segundo bufer de la conservada
 * (el stencil que hereda solo es aproximado fuera de la zona que comprueba la mascara: sin el bit
 * de los texeles; las llamas de los motores dejaban pasar la linea del HUD encima) */
static const char *SVS =
    "#version 300 es\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *SFS =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D u_msk;\n"
    "out vec4 o;\n"
    "void main() { if (texelFetch(u_msk, ivec2(gl_FragCoord.xy), 0).a < 0.5) discard; o = vec4(0.0); }\n";
static void stencil_from_mask(GLuint mask, int ow, int oh) {
    if (!g_cprog) copy_init();
    if (!g_sprog) {
        g_sprog = glCreateProgram();
        glAttachShader(g_sprog, shader(GL_VERTEX_SHADER, SVS));
        glAttachShader(g_sprog, shader(GL_FRAGMENT_SHADER, SFS));
        glLinkProgram(g_sprog);
    }
    glViewport(0, 0, ow, oh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glUseProgram(g_sprog);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mask);
    glUniform1i(glGetUniformLocation(g_sprog, "u_msk"), 2);
    static const float q[12] = {-1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, 1};
    glBindVertexArray(g_cvao);
    glBindBuffer(GL_ARRAY_BUFFER, g_cvbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glActiveTexture(GL_TEXTURE0);
}

static void read_zones(const PsxHwList *l, int scale) {
    for (int k = 0; k < l->nread; k++) {
        int x1 = l->read[k][0], y1 = l->read[k][1], x2 = l->read[k][2], y2 = l->read[k][3];
        Target *t = NULL;
        for (int i = 0; i < NTARGET; i++)
            if (g_tg[i].fbo && g_tg[i].valid && g_tg[i].scale == scale && g_tg[i].x0 == x1 && g_tg[i].y0 == y1 &&
                g_tg[i].w == x2 - x1 + 1 && g_tg[i].h == y2 - y1 + 1) t = &g_tg[i];
        if (!t) continue;
        if (!g_cprog) copy_init();
        glBindFramebuffer(GL_FRAMEBUFFER, g_vfbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_vram, 0);
        glViewport(0, 0, 1024, 512);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glUseProgram(g_cprog);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, t->tex);
        glUniform1i(u_csrc, 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, t->mask);
        glUniform1i(u_cmsk, 2);
        glUniform2i(u_corg, t->x0, t->y0);
        glUniform1i(u_cscale, scale);
        float X1 = (float)x1, Y1 = (float)y1, X2 = (float)(x2 + 1), Y2 = (float)(y2 + 1);
        float q[12] = {X1, Y1, X2, Y1, X1, Y2, X2, Y1, X2, Y2, X1, Y2};
        glBindVertexArray(g_cvao);
        glBindBuffer(GL_ARRAY_BUFFER, g_cvbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
        if (getenv("PSX_HW_COPYDBG")) fprintf(stderr, "psx_hw: antes de dibujar, error GL %#x\n", glGetError());
        glDrawArrays(GL_TRIANGLES, 0, 6);
        if (getenv("PSX_HW_COPYDBG")) {
            fprintf(stderr, "psx_hw: tras dibujar, error GL %#x\n", glGetError());
            GLuint px[4] = {0};
            glReadPixels(x1 + 100, y1 + 100, 1, 1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, px);
            uint8_t c[4] = {0};
            glBindFramebuffer(GL_READ_FRAMEBUFFER, t->fbo);
            glReadPixels(100 * scale, 100 * scale, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
            fprintf(stderr, "psx_hw: copia (%d,%d): vram %04x, imagen %d,%d,%d; error GL %#x, fbo %#x\n", x1, y1, px[0], c[0], c[1], c[2], glGetError(),
                    glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
        }
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        /* la copia de lo que tiene la textura ya no vale ahi: que se vuelva a subir la proxima vez */
        uint16_t *sh = g_shadow[g_ring];
        if (sh)
            for (int y = y1; y <= y2 && y < 512; y++)
                for (int x = x1; x <= x2 && x < 1024; x++) sh[y * 1024 + x] = (uint16_t)~sh[y * 1024 + x];
        psx_hw_stat_uploads++;
    }
}

static void frame(const PsxHwList *l, const uint16_t *bg, int x0, int y0, int w, int h, int scale, int real, int stale);
void psx_hw_frame(const PsxHwList *l, const uint16_t *bg, int x0, int y0, int w, int h, int scale) {
    frame(l, bg, x0, y0, w, h, scale, 0, 0);
}
void psx_hw_frame_ex(const PsxHwList *l, const uint16_t *bg, int x0, int y0, int w, int h, int scale, int real) {
    frame(l, bg, x0, y0, w, h, scale, real, l->main_stale);
}

static void frame(const PsxHwList *l, const uint16_t *bg, int x0, int y0, int w, int h, int scale, int real, int stale) {
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
    /* destino: la imagen conservada (real) o g_out (intermedia, o sin conservar). Base: la VRAM del
     * software, o la imagen conservada si el software no la tiene al dia */
    Target *tg = real || stale ? target(x0, y0, w, h, scale) : NULL;
    int keep = tg && stale && tg->valid;   /* se parte de la imagen conservada */
    GLuint dst = real ? tg->fbo : g_fbo;
    if (keep && !real) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, tg->fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbo);
        glBlitFramebuffer(0, 0, ow, oh, 0, 0, ow, oh, GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
    }
    g_last_tex = real ? tg->tex : g_out;
    glBindFramebuffer(GL_FRAMEBUFFER, dst);
    if (keep) stencil_from_mask(tg->mask, ow, oh);   /* el bit 15 exacto de la imagen de partida */
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
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
    glDisable(GL_STENCIL_TEST);
    g_asc[0] = g_asc[1] = g_asc[2] = g_asc[3] = -1;
    g_acls = 0; g_apass = -1; g_ast = -1;
    g_n = 0; g_ncmd = 0;
    g_track = 0;
    g_cx1 = g_cy1 = 1 << 20; g_cx2 = g_cy2 = -(1 << 20); g_clast = -1;
    for (int i = 0; i < l->n; i++)
        if ((l->p[i].flags & PSX_HWF_MASKCHK) && l->p[i].kind != PSX_HW_UPLOAD) {
            int x1, y1, x2, y2;
            bbox(&l->p[i], &x1, &y1, &x2, &y2);
            if (x1 < g_cx1) g_cx1 = x1;
            if (y1 < g_cy1) g_cy1 = y1;
            if (x2 > g_cx2) g_cx2 = x2;
            if (y2 > g_cy2) g_cy2 = y2;
            g_clast = i;
            g_track = 1;
        }
    static int nomask = -1;   /* prueba del comparador (caso positivo conocido): sin bit de mascara */
    if (nomask < 0) nomask = getenv("PSX_HW_NOMASK") != NULL;
    if (nomask) g_track = 0;
    /* fondo y texturas de partida: la VRAM de antes de la lista (con su bit 15 en el stencil) */
    upload(bg);
    if (l->nread) {   /* lo que solo tiene la GPU y la lista lee, a la textura; despues, el estado de antes */
        read_zones(l, scale);
        glBindFramebuffer(GL_FRAMEBUFFER, dst);
        glViewport(0, 0, ow, oh);
        glUseProgram(g_prog);
        glBindVertexArray(g_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, g_vram);
    }
    int32_t t[4] = {0, 0, F_DIRECT << 4, 0};
    if (tg) g_track = 1;   /* imagen conservada: su stencil debe seguir al bit 15 para la imagen siguiente */
    for (int pass = g_track ? 1 : 0; !keep && pass <= (g_track ? 2 : 0); pass++) {
        state(0, 0, -1, -1, 0, pass, g_track ? pass == 2 : -1);
        quad((float)x0, (float)y0, (float)(x0 + w), (float)(y0 + h), (float)x0, (float)y0, (float)(x0 + w), (float)(y0 + h), 0, 0, 0, t);
    }
    flush();
    /* primitivas; las UPLOAD actualizan la textura en su sitio */
    for (int i = 0; i < l->n; i++) draw_prim(&l->p[i], i);
    flush();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (real) tg->valid = 1;
    g_read_fbo = real ? tg->fbo : g_fbo;
}

unsigned psx_hw_texture(int *w, int *h) {
    if (w) *w = g_ow;
    if (h) *h = g_oh;
    return g_last_tex;
}

/* pruebas: el bit 15 que guarda la imagen conservada de la ultima lista dibujada (1 byte por pixel) */
int psx_hw_read_mask(uint8_t *out) {
    Target *t = NULL;
    for (int i = 0; i < NTARGET; i++) if (g_tg[i].fbo == g_read_fbo) t = &g_tg[i];
    if (!t) return -1;
    static uint32_t *tmp;
    if (!tmp) tmp = malloc(1024 * 512 * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, t->fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(0, 0, g_ow, g_oh, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    for (int i = 0; i < g_ow * g_oh; i++) out[i] = (tmp[i] >> 24) > 127;
    return 0;
}

void psx_hw_read(uint32_t *out) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_read_fbo);
    glReadPixels(0, 0, g_ow, g_oh, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
