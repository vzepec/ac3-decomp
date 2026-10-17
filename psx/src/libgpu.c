#include <stdio.h>
/* libgpu: funciones que solo escriben en memoria. Reimplementadas a partir de ACE.BIN EU
 * (direcciones en tools/psyqcheck_gpu.py) y validadas contra el codigo original. */
#include "psx_libgpu.h"
#include "psx_libetc.h"
#include "psx_gpu.h"
#include "psx_mem.h"

static int g_video_mode;            /* 0x800B4034 en ACE.BIN EU */
static int16_t g_vram_w = 1024, g_vram_h = 512;   /* 0x800B97E8 / 0x800B97EA */

int SetVideoMode(int mode) { int old = g_video_mode; g_video_mode = mode; return old; }
int GetVideoMode(void) { return g_video_mode; }
void psx_gpu_set_vram_size(int w, int h) { g_vram_w = (int16_t)w; g_vram_h = (int16_t)h; }

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h) {
    int pal = GetVideoMode();
    env->clip.x = (int16_t)x;
    env->clip.y = (int16_t)y;
    env->clip.w = (int16_t)w;
    env->tw.x = env->tw.y = env->tw.w = env->tw.h = 0;
    env->r0 = env->g0 = env->b0 = 0;
    env->dtd = 1;
    env->clip.h = (int16_t)h;
    env->dfe = (uint8_t)(h < (pal ? 289 : 257));
    env->ofs[0] = (int16_t)x;
    env->ofs[1] = (int16_t)y;
    env->tpage = 10;
    env->isbg = 0;
    return env;
}

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h) {
    env->disp.x = (int16_t)x;
    env->disp.y = (int16_t)y;
    env->disp.w = (int16_t)w;
    env->screen.x = env->screen.y = env->screen.w = env->screen.h = 0;
    env->isrgb24 = env->isinter = env->pad1 = env->pad0 = 0;
    env->disp.h = (int16_t)h;
    return env;
}

void SetDrawTPage(DR_TPAGE *p, int dfe, int dtd, int tpage) {
    ((uint8_t *)&p->tag)[3] = 1;
    uint32_t c = dtd ? 0xE1000200u : 0xE1000000u;
    uint32_t t = (uint32_t)tpage & 0x9FF;
    if (dfe) t |= 0x400;
    p->code[0] = c | t;
}

/* comandos E3/E4: x e y limitados a [0, tamano de la VRAM - 1] */
static uint32_t area_cmd(uint32_t cmd, int x, int y) {
    int16_t sx = (int16_t)x, sy = (int16_t)y;
    int32_t cx = sx < 0 ? 0 : (sx > g_vram_w - 1 ? g_vram_w - 1 : sx);
    int32_t cy = sy < 0 ? 0 : (sy > g_vram_h - 1 ? g_vram_h - 1 : sy);
    return cmd | (((uint32_t)cy & 0x3FF) << 10) | ((uint32_t)cx & 0x3FF);
}

void SetDrawArea(DR_AREA *p, RECT *r) {
    ((uint8_t *)&p->tag)[3] = 2;
    p->code[0] = area_cmd(0xE3000000u, r->x, r->y);
    p->code[1] = area_cmd(0xE4000000u, (int16_t)((uint16_t)r->x + (uint16_t)r->w - 1),
                          (int16_t)((uint16_t)r->y + (uint16_t)r->h - 1));
}

void CatPrim(void *p0, void *p1) {
    uint32_t *t = (uint32_t *)p0;
    *t = (*t & 0xFF000000u) | (psx_addr(p1) & 0xFFFFFF);
}

uint16_t GetTPage(int tp, int abr, int x, int y) {
    return (uint16_t)(((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
}

/* ---- funciones que usan la GPU ---- */

static PsxGpu g_gpu;
static int g_disp_x, g_disp_y, g_disp_w = 320, g_disp_h = 240, g_disp_on, g_disp_24, g_disp_inter;

void *psx_gpu(void) { return &g_gpu; }

int ResetGraph(int mode) {
    if (mode == 0 || mode == 3) psx_gpu_reset(&g_gpu);
    g_vram_w = 1024;
    g_vram_h = 512;
    return 0;
}

int DrawSync(int mode) { (void)mode; return 0; }   /* el dibujo es sincrono: nada pendiente */

int SetDispMask(int mask) { g_disp_on = mask != 0; return 0; }

int LoadImage(RECT *r, uint32_t *p) {
    psx_gpu_load_image(&g_gpu, r->x, r->y, r->w, r->h, (const uint16_t *)p);
    return 0;
}

int StoreImage(RECT *r, uint32_t *p) {
    psx_gpu_store_image(&g_gpu, r->x, r->y, r->w, r->h, (uint16_t *)p);
    return 0;
}

int ClearImage(RECT *r, uint8_t r0, uint8_t g0, uint8_t b0) {
    psx_gpu_fill(&g_gpu, r->x, r->y, r->w, r->h, r0, g0, b0);
    return 0;
}

/* Tabla de ordenacion invertida: ot[i] apunta a ot[i-1]; ot[0] es el final de la lista.
 * El original (0x800A6B50) lo hace con el DMA OTC y enlaza ot[0] a un paquete interno de
 * la libreria (0x800B98A4); aqui ot[0] = 0xFFFFFF, el terminador estandar (mismo efecto
 * al dibujar; SIN VERIFICAR si algun codigo del juego mira ese paquete). */
uint32_t *ClearOTagR(uint32_t *ot, int n) {
    for (int i = n - 1; i > 0; i--) ot[i] = psx_addr(&ot[i - 1]) & 0xFFFFFF;
    ot[0] = 0xFFFFFF;
    return ot;
}

uint32_t *ClearOTag(uint32_t *ot, int n) {
    for (int i = 0; i < n - 1; i++) ot[i] = psx_addr(&ot[i + 1]) & 0xFFFFFF;
    ot[n - 1] = 0xFFFFFF;
    return ot;
}

void DrawOTag(uint32_t *ot) { psx_gpu_draw_list(&g_gpu, psx_addr(ot)); }

DRAWENV *PutDrawEnv(DRAWENV *env) {
    int x2 = env->clip.x + env->clip.w - 1, y2 = env->clip.y + env->clip.h - 1;
    psx_gpu_gp0(&g_gpu, area_cmd(0xE3000000u, env->clip.x, env->clip.y));
    psx_gpu_gp0(&g_gpu, area_cmd(0xE4000000u, x2, y2));
    psx_gpu_gp0(&g_gpu, 0xE5000000u | (((uint32_t)env->ofs[1] & 0x7FF) << 11) | ((uint32_t)env->ofs[0] & 0x7FF));
    psx_gpu_gp0(&g_gpu, 0xE2000000u | ((uint32_t)(env->tw.w ? (~(env->tw.w - 1) >> 3) & 31 : 0)) |
                            ((uint32_t)(env->tw.h ? (~(env->tw.h - 1) >> 3) & 31 : 0) << 5) |
                            ((uint32_t)((env->tw.x >> 3) & 31) << 10) | ((uint32_t)((env->tw.y >> 3) & 31) << 15));
    psx_gpu_gp0(&g_gpu, 0xE1000000u | (env->tpage & 0x9FF) | (env->dtd ? 0x200u : 0) | (env->dfe ? 0x400u : 0));
    if (env->isbg) psx_gpu_fill(&g_gpu, env->clip.x, env->clip.y, env->clip.w, env->clip.h, env->r0, env->g0, env->b0);
    return env;
}

DISPENV *PutDispEnv(DISPENV *env) {
    g_disp_x = env->disp.x;
    g_disp_y = env->disp.y;
    g_disp_w = env->disp.w;
    g_disp_h = env->disp.h;
    g_disp_24 = env->isrgb24;
    g_disp_inter = env->isinter;
    return env;
}

void psx_gpu_display_mode(int *rgb24, int *interlaced) { *rgb24 = g_disp_24; *interlaced = g_disp_inter; }

void psx_gpu_display(int *x, int *y, int *w, int *h, int *enabled) {
    *x = g_disp_x; *y = g_disp_y; *w = g_disp_w; *h = g_disp_h; *enabled = g_disp_on;
}

/* ---- TIM en memoria (0x800A9218/0x800A9228 y su lector 0x800A9574 en EU) ---- */
static uint32_t g_tim;      /* siguiente TIM (0x800C9A30 en la original) */
static int g_graph_debug;

int SetGraphDebug(int level) { int o = g_graph_debug; g_graph_debug = level; return o; }
int GetGraphDebug(void) { return g_graph_debug; }

int OpenTIM(uint32_t *addr) { g_tim = psx_addr(addr); return 0; }

TIM_IMAGE *ReadTIM(TIM_IMAGE *t) {
    uint32_t p = g_tim;
    if (*(uint32_t *)psx_ptr(p) != 0x10) return 0;
    p += 4;
    t->mode = *(uint32_t *)psx_ptr(p);
    p += 4;
    if (g_graph_debug == 2) printf("id  =%08x\nmode=%08x\ntimaddr=%08x\n", 0x10u, t->mode, p);
    uint32_t cw = 0;
    if (t->mode & 8) {
        uint32_t n = *(uint32_t *)psx_ptr(p);
        t->crect = p + 4;
        t->caddr = p + 12;
        cw = n >> 2;
        p += cw * 4;
    } else {
        t->crect = t->caddr = 0;
    }
    uint32_t n = *(uint32_t *)psx_ptr(p);
    t->prect = p + 4;
    t->paddr = p + 12;
    g_tim += (cw + (n >> 2) + 2) * 4;
    return t;
}

int ClearImage2(RECT *r, uint8_t r0, uint8_t g0, uint8_t b0) { return ClearImage(r, r0, g0, b0); }
int StoreImage2(RECT *r, uint32_t *p) { return StoreImage(r, p); }

int MoveImage(RECT *r, int x, int y) {
    if (!r->w) return -1;
    if (!r->h) return 0;
    psx_gpu_gp0(&g_gpu, 0x80000000u);
    psx_gpu_gp0(&g_gpu, ((uint32_t)(uint16_t)r->y << 16) | (uint16_t)r->x);
    psx_gpu_gp0(&g_gpu, ((uint32_t)(uint16_t)y << 16) | (uint16_t)x);
    psx_gpu_gp0(&g_gpu, ((uint32_t)(uint16_t)r->h << 16) | (uint16_t)r->w);
    return 0;
}

int GetODE(void) { return VSync(-1) & 1; }

/* SetDrawMode (0x800AAC38): E1 (pagina, tramado, dibujo en el area visible) y E2 (ventana) */
void SetDrawMode(DR_MODE *p, int dfe, int dtd, int tpage, RECT *tw) {
    ((uint8_t *)&p->tag)[3] = 2;
    p->code[0] = (dtd ? 0xE1000200u : 0xE1000000u) | ((uint32_t)tpage & 0x9FF) | (dfe ? 0x400u : 0);
    if (tw)
        p->code[1] = (uint32_t)(uint8_t)((uint8_t)tw->y >> 3) << 15 | (uint32_t)(uint8_t)((uint8_t)tw->x >> 3) << 10 |
                     0xE2000000u | ((uint32_t)(-(int)tw->h) & 0xF8u) << 2 | ((uint32_t)(-(int)tw->w) & 0xFFu) >> 3;
    else
        p->code[1] = 0;
}
