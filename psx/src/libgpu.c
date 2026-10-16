/* libgpu: funciones que solo escriben en memoria. Reimplementadas a partir de ACE.BIN EU
 * (direcciones en tools/psyqcheck_gpu.py) y validadas contra el codigo original. */
#include "psx_libgpu.h"
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
