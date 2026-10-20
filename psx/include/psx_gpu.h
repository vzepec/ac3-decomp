/* GPU de la PlayStation por software (etapa 5): VRAM de 1024x512 y procesador de
 * comandos GP0 segun psx-spx (nocash). Rasteriza en la VRAM simulada; la salida a la
 * pantalla de Android (GLES) se construira encima.
 * Estado de validacion: pruebas de psx/test/test_gpu.c (texturas frente a los TIM del
 * disco, reglas de relleno, mezclas). La interpolacion de color y UV sigue la idea del
 * hardware pero no se ha verificado contra una PS1 real. */
#ifndef PSX_GPU_H
#define PSX_GPU_H

#include <stdint.h>

#define PSX_VRAM_W 1024
#define PSX_VRAM_H 512

typedef struct {
    uint16_t vram[PSX_VRAM_W * PSX_VRAM_H];
    /* estado de dibujo (GP0 E1-E6) */
    int tp_x, tp_y, tp_depth, abr;      /* pagina de textura: base x/64, y/256, 0=4b 1=8b 2=15b; mezcla */
    int dither, draw_display, tex_disable;
    int tw_mask_x, tw_mask_y, tw_off_x, tw_off_y;
    int area_x1, area_y1, area_x2, area_y2;
    int off_x, off_y;
    int mask_set, mask_check;
    /* transferencia CPU -> VRAM en curso (GP0 A0) */
    int xfer_x, xfer_y, xfer_w, xfer_h, xfer_pos, xfer_active;
    /* comando en ensamblado (los comandos pueden llegar partidos entre paquetes) */
    uint32_t cmd[16];
    int cmd_len, cmd_need;
    /* polilinea en curso */
    int poly_line;
    /* contadores para pruebas */
    uint32_t prims_drawn, pixels_drawn;
} PsxGpu;

void psx_gpu_reset(PsxGpu *g);
/* Envia una palabra al puerto GP0 (comandos de dibujo y datos de transferencia). */
void psx_gpu_gp0(PsxGpu *g, uint32_t w);
/* Recorre una lista de dibujo (tabla de ordenacion) en la RAM de la PS1 desde la
 * direccion `addr` hasta el terminador 0xFFFFFF, enviando cada paquete a GP0. */
void psx_gpu_draw_list(PsxGpu *g, uint32_t addr);
/* Igual, leyendo los paquetes de copias de la RAM (2 MB) y del scratchpad (1 KB). */
void psx_gpu_draw_list_mem(PsxGpu *g, uint32_t addr, const uint8_t *ram, const uint8_t *scratch);
/* Acceso directo a la VRAM (LoadImage/StoreImage/ClearImage de libgpu). */
void psx_gpu_load_image(PsxGpu *g, int x, int y, int w, int h, const uint16_t *src);

/* ---- registro de primitivas (etapa 6, paso 7: dibujo por GLES a alta resolucion) ----
 * Con un registro activo en el hilo (psx_gpu_record), cada primitiva que la GPU dibuja se anota
 * ya interpretada (coordenadas con el desplazamiento aplicado, color, texturas, estado), despues
 * del descarte del hardware. Con only = 1 se interpreta sin dibujar pixeles. */
/* VRAM: copia o carga cuyo destino esta en el area principal (se dibuja desde la VRAM: origen en
 * u/v o, para una carga, los datos de una UPLOAD anterior en el mismo sitio); UPLOAD: el contenido
 * de la VRAM del software en ese momento, en un rectangulo fuera del area principal (paletas y
 * texturas que la lista cambia o dibuja): datos en list->data[data_off] */
enum { PSX_HW_TRI = 0, PSX_HW_LINE = 1, PSX_HW_RECT = 2, PSX_HW_FILL = 3, PSX_HW_VRAM = 4, PSX_HW_UPLOAD = 5 };
enum { PSX_HWF_TEX = 1, PSX_HWF_RAW = 2, PSX_HWF_SEMI = 4, PSX_HWF_GOURAUD = 8, PSX_HWF_DITHER = 16,
       PSX_HWF_MASKSET = 32, PSX_HWF_MASKCHK = 64, PSX_HWF_OFFSCREEN = 128 /* fuera del area principal: no se dibuja */ };
typedef struct {
    uint8_t kind, flags, abr, depth;           /* depth: 0 = 4 bits, 1 = 8 bits, 2 = 15 bits */
    int16_t x[3], y[3];                        /* RECT/FILL/VRAM: x[0],y[0] origen, x[1],y[1] ancho y alto */
    uint8_t r[3], g[3], b[3];
    uint8_t u[3], v[3];
    uint16_t tp_x, tp_y, clut_x, clut_y;       /* pagina en pixeles de la VRAM (x * 64, y * 256) */
    uint8_t tw_mx, tw_my, tw_ox, tw_oy;        /* ventana de textura (unidades de 8 texeles) */
    int16_t ax1, ay1, ax2, ay2;                /* area de dibujo (inclusiva) */
    uint32_t data_off;                         /* UPLOAD: desplazamiento en list->data */
} PsxHwPrim;
/* main_*: area principal (la de la imagen, inclusiva), la fija quien llama antes de dibujar */
typedef struct {
    PsxHwPrim *p;
    int n, max;
    uint16_t *data;
    int ndata, maxdata;
    int main_x1, main_y1, main_x2, main_y2;
    int dirty, dx1, dy1, dx2, dy2;             /* zona sucia (interno) */
    int blit;                                  /* la zona sucia toca el area principal: tras subirla se dibuja */
    int main_stale;                            /* la imagen del software no esta al dia: la GPU parte de su copia */
    int nread;                                 /* zonas que solo tiene la GPU y que la lista lee como textura */
    int16_t read[4][4];                        /* (x1, y1, x2, y2): la GPU las copia a su textura de la VRAM antes */
} PsxHwList;
void psx_gpu_record(PsxHwList *list, int only);   /* NULL: sin registro (por hilo) */
void psx_gpu_record_flush(PsxGpu *g);              /* al terminar la lista: lo pendiente */
/* etapa 8, paso 2. Con only, la imagen (el area de dibujo al empezar la lista) no se dibuja en la
 * VRAM del software: queda solo en la GPU (psx/gl/psx_hw.c la conserva). psx_gpu_draw_list lo
 * anota por hilo (el del juego); psx_gpu_main_stale dice si una zona esta asi. Lo que lee o
 * escribe esas zonas por otro camino se cuenta en psx_gpu_stale_ev: [0] texturas o paletas,
 * [1] origen de copias, [2] StoreImage, [3] escrituras fuera de las listas (estas se pasan a la
 * GPU con la siguiente lista anotada) */
int psx_gpu_main_stale(int x1, int y1, int x2, int y2);
void psx_gpu_main_mark(int x1, int y1, int x2, int y2, int stale);
extern long psx_gpu_stale_ev[4];
/* la lista copia (VRAM -> VRAM) desde ese rectangulo (inclusivo); no dibuja nada */
int psx_gpu_list_copies_from(uint32_t ot, const uint8_t *ram, const uint8_t *scratch, int x1, int y1, int x2, int y2);
void psx_gpu_store_image(PsxGpu *g, int x, int y, int w, int h, uint16_t *dst);
void psx_gpu_fill(PsxGpu *g, int x, int y, int w, int h, uint8_t r, uint8_t gr, uint8_t b);

#endif
