/* Capa de compatibilidad PsyQ (etapa 5): libgpu. Estructuras con la disposicion de
 * PsyQ 4.x; los punteros entre paquetes son direcciones PS1 (psx_mem.h). */
#ifndef PSX_LIBGPU_H
#define PSX_LIBGPU_H

#include <stdint.h>

typedef struct { int16_t x, y, w, h; } RECT;

typedef struct {
    uint32_t tag;
    uint32_t code[15];
} DR_ENV;

typedef struct {
    RECT clip;          /* 0 */
    int16_t ofs[2];     /* 8 */
    RECT tw;            /* 12 */
    uint16_t tpage;     /* 20 */
    uint8_t dtd;        /* 22 */
    uint8_t dfe;        /* 23 */
    uint8_t isbg;       /* 24 */
    uint8_t r0, g0, b0; /* 25 */
    DR_ENV dr_env;      /* 28 */
} DRAWENV;              /* 92 bytes */

typedef struct {
    RECT disp;
    RECT screen;
    uint8_t isinter, isrgb24, pad0, pad1;
} DISPENV;              /* 20 bytes */

typedef struct { uint32_t tag; uint32_t code[1]; } DR_TPAGE;
typedef struct { uint32_t tag; uint32_t code[2]; } DR_AREA;

/* modo de video (0 NTSC, 1 PAL): SetVideoMode/GetVideoMode de libetc */
int SetVideoMode(int mode);
int GetVideoMode(void);
/* tamano de la VRAM que usa libgpu para limitar las areas de dibujo (ResetGraph lo fija) */
void psx_gpu_set_vram_size(int w, int h);

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h);
DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h);
void SetDrawTPage(DR_TPAGE *p, int dfe, int dtd, int tpage);
void SetDrawArea(DR_AREA *p, RECT *r);
void CatPrim(void *p0, void *p1);
uint16_t GetTPage(int tp, int abr, int x, int y);

#endif
