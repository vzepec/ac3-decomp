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

/* TIM en memoria (OpenTIM/ReadTIM): los punteros son direcciones PS1 dentro del TIM */
typedef struct { uint32_t mode, crect, caddr, prect, paddr; } TIM_IMAGE;
int OpenTIM(uint32_t *addr);
TIM_IMAGE *ReadTIM(TIM_IMAGE *timimg);
int SetGraphDebug(int level);
int GetGraphDebug(void);

/* modo de video (0 NTSC, 1 PAL): SetVideoMode/GetVideoMode de libetc */
int SetVideoMode(int mode);
int GetVideoMode(void);
/* tamano de la VRAM que usa libgpu para limitar las areas de dibujo (ResetGraph lo fija) */
void psx_gpu_set_vram_size(int w, int h);

/* ---- funciones que usan la GPU (psx/src/gpu.c) ---- */
struct PsxGpuTag;
void *psx_gpu(void);                 /* PsxGpu * global */
int ResetGraph(int mode);
int DrawSync(int mode);
int SetDispMask(int mask);
int LoadImage(RECT *r, uint32_t *p);
int StoreImage(RECT *r, uint32_t *p);
int ClearImage(RECT *r, uint8_t r0, uint8_t g0, uint8_t b0);
/* variantes: ClearImage2 (modo entrelazado: se rellena todo el rectangulo, SIN VERIFICAR
 * que la consola rellene solo un campo) y StoreImage2 (sincrona) */
int ClearImage2(RECT *r, uint8_t r0, uint8_t g0, uint8_t b0);
int StoreImage2(RECT *r, uint32_t *p);
/* copia de VRAM a VRAM (GP0 0x80) */
int MoveImage(RECT *r, int x, int y);
/* campo par/impar (GPUSTAT bit 31): modelo por fotogramas, alterna en cada VBlank */
int GetODE(void);
typedef struct { uint32_t tag; uint32_t code[2]; } DR_MODE;
void SetDrawMode(DR_MODE *p, int dfe, int dtd, int tpage, RECT *tw);
uint32_t *ClearOTagR(uint32_t *ot, int n);
uint32_t *ClearOTag(uint32_t *ot, int n);
void DrawOTag(uint32_t *ot);
DRAWENV *PutDrawEnv(DRAWENV *env);
DISPENV *PutDispEnv(DISPENV *env);
/* zona de la VRAM que se muestra (ultimo PutDispEnv) y si la pantalla esta activa */
void psx_gpu_display(int *x, int *y, int *w, int *h, int *enabled);

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h);
DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h);
void SetDrawTPage(DR_TPAGE *p, int dfe, int dtd, int tpage);
void SetDrawArea(DR_AREA *p, RECT *r);
void CatPrim(void *p0, void *p1);
uint16_t GetTPage(int tp, int abr, int x, int y);

#endif
