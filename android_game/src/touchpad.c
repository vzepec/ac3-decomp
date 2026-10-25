/* Mando virtual en pantalla (ver touchpad.h). */
#include "touchpad.h"

#include <GLES3/gl3.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "psx_libpad.h"

/* un control: centro y medio tamano en dp (desde la esquina superior izquierda; x < 0: desde la
 * derecha, y < 0: desde abajo), celda del atlas y lo que pulsa */
typedef struct {
    float cx, cy, hw, hh;
    int cell;
    uint16_t mask;
} Ctl;

enum { STICK = -1, HIDE = -2 };

/* disposicion para una pantalla apaisada (unos 700 x 400 dp en la Retroid): stick flotante abajo a la
 * izquierda, cruceta encima, botones de cara abajo a la derecha, gatillos arriba, START/SELECT abajo
 * en el centro (cx de 9999: centrado, se suma el desplazamiento de hw2) */
static const Ctl LAYOUT[] = {
    {-94, -156, 30, 30, TP_TRIANGLE, PSX_PAD_TRIANGLE},
    {-94, -52, 30, 30, TP_CROSS, PSX_PAD_CROSS},
    {-146, -104, 30, 30, TP_SQUARE, PSX_PAD_SQUARE},
    {-42, -104, 30, 30, TP_CIRCLE, PSX_PAD_CIRCLE},
    {52, 34, 34, 22, TP_L2, PSX_PAD_L2},
    {130, 34, 34, 22, TP_L1, PSX_PAD_L1},
    {-130, 34, 34, 22, TP_R1, PSX_PAD_R1},
    {-52, 34, 34, 22, TP_R2, PSX_PAD_R2},
    {9999 - 56, -24, 40, 16, TP_SELECT, PSX_PAD_SELECT},
    {9999 + 56, -24, 40, 16, TP_START, PSX_PAD_START},
    {96, 112, 22, 22, TP_UP, PSX_PAD_UP},
    {96, 184, 22, 22, TP_DOWN, PSX_PAD_DOWN},
    {60, 148, 22, 22, TP_LEFT, PSX_PAD_LEFT},
    {132, 148, 22, 22, TP_RIGHT, PSX_PAD_RIGHT},
    {9999, 24, 18, 14, TP_HIDE, 0},
};
#define NCTL (int)(sizeof LAYOUT / sizeof LAYOUT[0])
#define STICK_R 56.0f          /* recorrido del stick (dp) */
#define STICK_HOME_X 120.0f    /* donde se dibuja en reposo */
#define STICK_HOME_Y (-110.0f)
#define ALPHA 0.55f

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_mode, g_visible = 1, g_hidden_by_user;
static int g_w, g_h;
static float g_dp = 1.0f;      /* pixeles por dp */
/* dedos: control capturado (indice de LAYOUT, STICK o -100 = ninguno) */
#define NPTR 10
static struct { int id, ctl; float x0, y0, x, y; } g_ptr[NPTR];
static int g_nptr;
static uint16_t g_buttons;
static float g_sx, g_sy, g_ox, g_oy;
static int g_stick;

/* atlas */
static uint32_t *g_atlas;
static int g_aw, g_ah, g_atlas_new;
static GLuint g_tex, g_prog, g_vao, g_vbo;

void touchpad_set_mode(int mode) { g_mode = mode; g_visible = mode != 2; }

/* ARGB sin premultiplicar (Bitmap.getPixels) -> RGBA premultiplicado */
static uint32_t *premul(const uint32_t *argb, int w, int h) {
    uint32_t *a = malloc((size_t)w * h * 4);
    if (!a) return NULL;
    for (int i = 0; i < w * h; i++) {
        uint32_t c = argb[i], al = c >> 24;
        uint32_t r = ((c >> 16) & 255) * al / 255, g = ((c >> 8) & 255) * al / 255, b = (c & 255) * al / 255;
        a[i] = r | g << 8 | b << 16 | al << 24;
    }
    return a;
}

static uint32_t *g_label;
static int g_lw, g_lh, g_label_new;
static GLuint g_ltex;

void touchpad_set_label(const uint32_t *argb, int w, int h) {
    uint32_t *a = premul(argb, w, h);
    if (!a) return;
    pthread_mutex_lock(&g_mu);
    free(g_label);
    g_label = a; g_lw = w; g_lh = h; g_label_new = 1;
    pthread_mutex_unlock(&g_mu);
}

void touchpad_set_atlas(const uint32_t *argb, int w, int h) {
    uint32_t *a = premul(argb, w, h);
    if (!a) return;
    pthread_mutex_lock(&g_mu);
    free(g_atlas);
    g_atlas = a; g_aw = w; g_ah = h; g_atlas_new = 1;
    pthread_mutex_unlock(&g_mu);
}

void touchpad_set_screen(int w, int h, int dpi) {
    g_w = w; g_h = h;
    g_dp = dpi > 0 ? dpi / 160.0f : 1.0f;
    /* en pantallas pequenas en dp, todo un poco mas pequeno para que quepa */
    float hdp = h / g_dp;
    if (hdp < 360) g_dp *= hdp / 360.0f;
}

/* centro en pixeles de un control */
static void ctl_pos(const Ctl *c, float *x, float *y) {
    float cx = c->cx, cy = c->cy;
    if (cx > 5000) *x = g_w * 0.5f + (cx - 9999) * g_dp;
    else *x = cx < 0 ? g_w + cx * g_dp : cx * g_dp;
    *y = cy < 0 ? g_h + cy * g_dp : cy * g_dp;
}

/* el control bajo (x, y), con un margen para los dedos; -100 si ninguno */
static int hit(float x, float y, int slide_from) {
    int best = -100;
    float bd = 1e9f;
    for (int i = 0; i < NCTL; i++) {
        const Ctl *c = &LAYOUT[i];
        if (!g_visible && c->cell != TP_HIDE) continue;
        /* deslizando: solo entre los de su grupo (cara con cara, cruceta con cruceta) */
        if (slide_from >= 0) {
            int ga = LAYOUT[slide_from].cell <= TP_TRIANGLE ? 0 : LAYOUT[slide_from].cell >= TP_UP && LAYOUT[slide_from].cell <= TP_RIGHT ? 1 : 2;
            int gb = c->cell <= TP_TRIANGLE ? 0 : c->cell >= TP_UP && c->cell <= TP_RIGHT ? 1 : 2;
            if (ga != gb || ga == 2) continue;
        }
        float px, py;
        ctl_pos(c, &px, &py);
        float mx = (c->hw + 8) * g_dp, my = (c->hh + 8) * g_dp;
        float dx = fabsf(x - px), dy = fabsf(y - py);
        if (dx <= mx && dy <= my) {
            float d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = i; }
        }
    }
    return best;
}

static void recompute(void) {
    uint16_t b = 0;
    int st = 0;
    for (int k = 0; k < g_nptr; k++) {
        int c = g_ptr[k].ctl;
        if (c >= 0) b |= LAYOUT[c].mask;
        else if (c == STICK) {
            float dx = (g_ptr[k].x - g_ptr[k].x0) / (STICK_R * g_dp), dy = (g_ptr[k].y - g_ptr[k].y0) / (STICK_R * g_dp);
            float l = sqrtf(dx * dx + dy * dy);
            if (l > 1) { dx /= l; dy /= l; }
            g_sx = dx; g_sy = dy; g_ox = g_ptr[k].x0; g_oy = g_ptr[k].y0;
            st = 1;
        }
    }
    g_buttons = b;
    g_stick = st;
    if (!st) g_sx = g_sy = 0;
}

static void down(int id, float x, float y) {
    if (g_nptr >= NPTR) return;
    int c = hit(x, y, -1);
    if (c >= 0 && LAYOUT[c].cell == TP_HIDE) {   /* ocultar / mostrar */
        g_visible = !g_visible;
        g_hidden_by_user = !g_visible;
        c = -100;
    } else if (c < 0 && g_visible && x < g_w * 0.45f && y > g_h * 0.40f) c = STICK;   /* el stick, donde se apoye */
    else if (!g_visible && g_mode == 0 && !g_hidden_by_user) { g_visible = 1; c = hit(x, y, -1); }
    g_ptr[g_nptr].id = id; g_ptr[g_nptr].ctl = c;
    g_ptr[g_nptr].x0 = g_ptr[g_nptr].x = x; g_ptr[g_nptr].y0 = g_ptr[g_nptr].y = y;
    g_nptr++;
}

static void up(int id) {
    for (int k = 0; k < g_nptr; k++)
        if (g_ptr[k].id == id) { g_ptr[k] = g_ptr[--g_nptr]; return; }
}

int touchpad_event(const AInputEvent *e) {
    if (AInputEvent_getType(e) != AINPUT_EVENT_TYPE_MOTION || !(AInputEvent_getSource(e) & AINPUT_SOURCE_TOUCHSCREEN)) return 0;
    if (g_mode == 2) return 1;
    int32_t a = AMotionEvent_getAction(e), act = a & AMOTION_EVENT_ACTION_MASK;
    size_t idx = (size_t)((a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    pthread_mutex_lock(&g_mu);
    switch (act) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        down(AMotionEvent_getPointerId(e, idx), AMotionEvent_getX(e, idx), AMotionEvent_getY(e, idx));
        break;
    case AMOTION_EVENT_ACTION_MOVE:
        for (size_t i = 0; i < AMotionEvent_getPointerCount(e); i++) {
            int id = AMotionEvent_getPointerId(e, i);
            for (int k = 0; k < g_nptr; k++) {
                if (g_ptr[k].id != id) continue;
                g_ptr[k].x = AMotionEvent_getX(e, i);
                g_ptr[k].y = AMotionEvent_getY(e, i);
                if (g_ptr[k].ctl >= 0) {   /* deslizar a un vecino del mismo grupo */
                    int c = hit(g_ptr[k].x, g_ptr[k].y, g_ptr[k].ctl);
                    if (c >= 0) g_ptr[k].ctl = c;
                }
            }
        }
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
        up(AMotionEvent_getPointerId(e, idx));
        break;
    case AMOTION_EVENT_ACTION_CANCEL:
        g_nptr = 0;
        break;
    default: break;
    }
    recompute();
    pthread_mutex_unlock(&g_mu);
    return 1;
}

void touchpad_physical(void) {
    if (g_mode != 0 || !g_visible) return;
    pthread_mutex_lock(&g_mu);
    g_visible = 0;
    g_nptr = 0;
    recompute();
    pthread_mutex_unlock(&g_mu);
}

uint16_t touchpad_buttons(void) { return g_buttons; }

void touchpad_stick(float *x, float *y, int *active) {
    *x = g_sx; *y = g_sy; *active = g_stick;
}

/* ------------------------------------------------------------------ dibujo */

static const char *VS =
    "#version 300 es\n"
    "layout(location=0) in vec2 a_pos; layout(location=1) in vec2 a_uv; layout(location=2) in vec2 a_k;\n"
    "out vec2 v_uv; out vec2 v_k;\n"
    "void main() { v_uv = a_uv; v_k = a_k; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
/* a_k: (alfa, brillo): pulsado, mas opaco y mas claro */
static const char *FS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex; in vec2 v_uv; in vec2 v_k; out vec4 o;\n"
    "void main() { vec4 c = texture(u_tex, v_uv); c.rgb = mix(c.rgb, vec3(c.a), v_k.y); o = c * v_k.x; }\n";

static GLuint sh(GLenum t, const char *src) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

void touchpad_gl_lost(void) {
    g_tex = g_prog = g_vao = g_vbo = g_ltex = 0;
    pthread_mutex_lock(&g_mu);
    if (g_atlas) g_atlas_new = 1;
    if (g_label) g_label_new = 1;
    pthread_mutex_unlock(&g_mu);
}

static void prog_init(void) {
    if (g_prog) return;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, sh(GL_VERTEX_SHADER, VS));
    glAttachShader(g_prog, sh(GL_FRAGMENT_SHADER, FS));
    glLinkProgram(g_prog);
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 24, (void *)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 24, (void *)16);
    glBindVertexArray(0);
}

static float g_q[(NCTL + 2) * 6 * 6];
static int g_nq;

static void quad(float cx, float cy, float hw, float hh, int cell, float alpha, float bright) {
    if (g_nq >= (NCTL + 2)) return;
    float x1 = (cx - hw) / g_w * 2 - 1, x2 = (cx + hw) / g_w * 2 - 1;
    float y1 = 1 - (cy - hh) / g_h * 2, y2 = 1 - (cy + hh) / g_h * 2;
    int col = cell % TP_ATLAS_COLS, row = cell / TP_ATLAS_COLS;
    float u1 = (float)(col * TP_CELL) / g_aw, u2 = (float)((col + 1) * TP_CELL) / g_aw;
    float v1 = (float)(row * TP_CELL) / g_ah, v2 = (float)((row + 1) * TP_CELL) / g_ah;
    float v[6][6] = {{x1, y1, u1, v1, alpha, bright}, {x2, y1, u2, v1, alpha, bright}, {x1, y2, u1, v2, alpha, bright},
                     {x2, y1, u2, v1, alpha, bright}, {x2, y2, u2, v2, alpha, bright}, {x1, y2, u1, v2, alpha, bright}};
    memcpy(&g_q[g_nq * 36], v, sizeof v);
    g_nq++;
}

void touchpad_draw(void) {
    if (g_mode == 2 || !g_w || !g_h) return;
    pthread_mutex_lock(&g_mu);
    if (g_atlas_new && g_atlas) {
        if (!g_tex) glGenTextures(1, &g_tex);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_aw, g_ah, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_atlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_atlas_new = 0;
    }
    if (!g_tex) { pthread_mutex_unlock(&g_mu); return; }
    prog_init();
    g_nq = 0;
    for (int i = 0; i < NCTL; i++) {
        const Ctl *c = &LAYOUT[i];
        if (!g_visible && c->cell != TP_HIDE) continue;
        float x, y;
        ctl_pos(c, &x, &y);
        int on = c->mask && (g_buttons & c->mask);
        float s = fmaxf(c->hw, c->hh) * g_dp;   /* las celdas son cuadradas: el grafico va centrado */
        quad(x, y, s, s, c->cell, on ? 0.9f : (g_visible ? ALPHA : 0.3f), on ? 0.35f : 0.0f);
    }
    if (g_visible) {   /* el stick: base y mando, donde se apoyo el pulgar o en su sitio */
        float bx = g_stick ? g_ox : STICK_HOME_X * g_dp, by = g_stick ? g_oy : g_h + STICK_HOME_Y * g_dp;
        float r = (STICK_R + 12) * g_dp;
        quad(bx, by, r, r, TP_BASE, ALPHA, 0);
        quad(bx + g_sx * STICK_R * g_dp, by + g_sy * STICK_R * g_dp, 30 * g_dp, 30 * g_dp, TP_KNOB, g_stick ? 0.9f : ALPHA, 0);
    }
    pthread_mutex_unlock(&g_mu);
    glViewport(0, 0, g_w, g_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   /* el atlas viene premultiplicado */
    glUseProgram(g_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(glGetUniformLocation(g_prog, "u_tex"), 0);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_nq * 36 * sizeof(float), g_q, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_nq * 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void touchpad_draw_label(void) {
    if (!g_w || !g_h) return;
    pthread_mutex_lock(&g_mu);
    if (g_label_new && g_label) {
        if (!g_ltex) glGenTextures(1, &g_ltex);
        glBindTexture(GL_TEXTURE_2D, g_ltex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_lw, g_lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_label);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_label_new = 0;
    }
    int lw = g_lw, lh = g_lh;
    pthread_mutex_unlock(&g_mu);
    if (!g_ltex) return;
    prog_init();
    /* centrado, a su tamano en dp (como mucho el 80 % del ancho) */
    float w = lw * g_dp / 2.25f, h = lh * g_dp / 2.25f;   /* el rotulo se dibuja para 2,25 px por dp */
    if (w > g_w * 0.8f) { h *= g_w * 0.8f / w; w = g_w * 0.8f; }
    float x1 = -w / g_w, x2 = w / g_w, y1 = h / g_h, y2 = -h / g_h;
    float v[6][6] = {{x1, y1, 0, 0, 1, 0}, {x2, y1, 1, 0, 1, 0}, {x1, y2, 0, 1, 1, 0},
                     {x2, y1, 1, 0, 1, 0}, {x2, y2, 1, 1, 1, 0}, {x1, y2, 0, 1, 1, 0}};
    glViewport(0, 0, g_w, g_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_ltex);
    glUniform1i(glGetUniformLocation(g_prog, "u_tex"), 0);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}
