/* Presentacion con GLES 3 (ver psx_present.h). */
#include <GLES3/gl3.h>

#include "psx_present.h"

static GLuint g_prog, g_tex, g_vao, g_vbo, g_otex;
static int g_tw, g_th;

static const char *VS = "#version 300 es\n"
                        "layout(location=0) in vec2 p; layout(location=1) in vec2 t; out vec2 uv;\n"
                        "void main(){ uv=t; gl_Position=vec4(p,0.0,1.0); }\n";
static const char *FS = "#version 300 es\nprecision mediump float;\n"
                        "in vec2 uv; uniform sampler2D tex; out vec4 o;\n"
                        "void main(){ o=texture(tex,uv); }\n";

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}

int psx_present_init(void) {
    GLuint v = shader(GL_VERTEX_SHADER, VS), f = shader(GL_FRAGMENT_SHADER, FS);
    if (!v || !f) return -1;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, v);
    glAttachShader(g_prog, f);
    glLinkProgram(g_prog);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) return -1;
    glGenTextures(1, &g_tex);
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    return 0;
}

static int g_dx, g_dy, g_dw, g_dh, g_iw, g_ih;   /* ultimo recuadro de destino y tamano de la imagen */
static void draw_tex(GLuint tex, int w, int h, int vw, int vh, int fit, int linear);

void psx_present_frame(const uint32_t *rgba, int w, int h, int vw, int vh, int fit, int linear) {
    if (w <= 0 || h <= 0) return;
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (w != g_tw || h != g_th) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        g_tw = w;
        g_th = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    draw_tex(g_tex, w, h, vw, vh, fit, linear);
}

void psx_present_texture(unsigned tex, int w, int h, int vw, int vh, int fit, int linear) {
    if (w <= 0 || h <= 0 || !tex) return;
    draw_tex(tex, w, h, vw, vh, fit, linear);
}

static void draw_tex(GLuint tex, int w, int h, int vw, int vh, int fit, int linear) {
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint flt = linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, flt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, flt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* rectangulo de destino en pixeles de la ventana */
    int dx, dy, dw, dh;
    if (fit == PSX_FIT_EXACT) {
        dx = 0; dy = vh - h; dw = w; dh = h;
    } else if (fit == PSX_FIT_INTEGER) {
        int k = vw / w < vh / h ? vw / w : vh / h;
        if (k < 1) k = 1;
        dw = w * k; dh = h * k; dx = (vw - dw) / 2; dy = (vh - dh) / 2;
    } else if (fit == PSX_FIT_169) {   /* pantalla ancha: la escena comprimida por la GTE, a 16:9 */
        dw = vw; dh = vw * 9 / 16;
        if (dh > vh) { dh = vh; dw = vh * 16 / 9; }
        dx = (vw - dw) / 2; dy = (vh - dh) / 2;
    } else {   /* 4:3: el televisor muestra la imagen a 4:3 sea cual sea su ancho en pixeles */
        dw = vw; dh = vw * 3 / 4;
        if (dh > vh) { dh = vh; dw = vh * 4 / 3; }
        dx = (vw - dw) / 2; dy = (vh - dh) / 2;
    }
    glViewport(0, 0, vw, vh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(dx, dy, dw, dh);
    /* la fila 0 de la imagen va arriba */
    static const float quad[] = {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0};
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    glUseProgram(g_prog);
    glUniform1i(glGetUniformLocation(g_prog, "tex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_dx = dx; g_dy = dy; g_dw = dw; g_dh = dh;
}

/* superpone rgba (w x h, alfa 0 = transparente) en la esquina superior izquierda de la ultima
 * imagen presentada, a la escala de una imagen de ref_w x ref_h pixeles de la PS1 */
void psx_present_overlay(const uint32_t *rgba, int w, int h, int ref_w, int ref_h) {
    if (!g_dw || !g_dh || ref_w <= 0 || ref_h <= 0) return;
    if (!g_otex) glGenTextures(1, &g_otex);
    glBindTexture(GL_TEXTURE_2D, g_otex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    int ow = g_dw * w / ref_w, oh = g_dh * h / ref_h;
    glViewport(g_dx, g_dy + g_dh - oh, ow, oh);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
}

void psx_present_shutdown(void) {
    if (g_tex) glDeleteTextures(1, &g_tex);
    if (g_otex) glDeleteTextures(1, &g_otex);
    g_otex = 0;
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_vao) glDeleteVertexArrays(1, &g_vao);
    if (g_prog) glDeleteProgram(g_prog);
    g_tex = g_vbo = g_vao = g_prog = 0;
    g_tw = g_th = 0;
}
