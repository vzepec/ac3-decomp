/* libetc y modelo de tiempo de la capa (ver psx_libetc.h). */
#include "psx_libetc.h"

void psx_cd_frame(void);    /* libcd.c */
void psx_pad_frame(void);   /* libpad.c */

static PsxCallFn g_call;
static void (*g_wait)(void);
static uint32_t g_vsync_cb, g_dma_cb[8];
static int g_hz = 50, g_in_callback, g_intr_mask;
static uint32_t g_vcount, g_last;   /* contador de VBlank y valor en la ultima llamada a VSync */

void psx_set_call(PsxCallFn fn) { g_call = fn; }
void psx_call(uint32_t addr, uint32_t a0, uint32_t a1) { if (addr && g_call) g_call(addr, a0, a1); }
void psx_set_frame_wait(void (*fn)(void)) { g_wait = fn; }
void psx_set_refresh(int hz) { g_hz = hz == 60 ? 60 : 50; }
int psx_refresh(void) { return g_hz; }

void psx_vblank(void) {
    if (g_wait) g_wait();
    g_vcount++;
    psx_pad_frame();
    psx_cd_frame();
    /* el callback de VSync no se reentra (en la PS1 las interrupciones estan cerradas) */
    if (g_vsync_cb && !g_in_callback) {
        g_in_callback = 1;
        psx_call(g_vsync_cb, 0, 0);
        g_in_callback = 0;
    }
}

/* VSync(-1): contador de VBlank. VSync(0): espera al siguiente. VSync(1): no espera.
 * VSync(n>1): espera hasta que pasen n VBlank desde la llamada anterior.
 * 0 y 1 devuelven las lineas horizontales desde la llamada anterior (313 por fotograma
 * en PAL, 263 en NTSC: aproximacion por fotogramas enteros). */
int VSync(int mode) {
    if (mode < 0) return (int)g_vcount;
    uint32_t start = g_last;
    if (mode == 0) psx_vblank();
    else if (mode > 1) while (g_vcount - start < (uint32_t)mode) psx_vblank();
    int lines = (int)(g_vcount - start) * (g_hz == 50 ? 313 : 263);
    g_last = g_vcount;
    return lines;
}

int VSyncCallback(uint32_t func) { uint32_t old = g_vsync_cb; g_vsync_cb = func; return (int)old; }
int ResetCallback(void) { g_vsync_cb = 0; for (int i = 0; i < 8; i++) g_dma_cb[i] = 0; return 0; }
int StopCallback(void) { return 0; }
int CheckCallback(void) { return g_in_callback; }
int DMACallback(int dma, uint32_t func) {
    if (dma < 0 || dma > 7) return 0;
    uint32_t old = g_dma_cb[dma];
    g_dma_cb[dma] = func;
    return (int)old;
}
int SetIntrMask(int mask) { int old = g_intr_mask; g_intr_mask = mask; return old; }
