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
/* Acceso directo a la VRAM (LoadImage/StoreImage/ClearImage de libgpu). */
void psx_gpu_load_image(PsxGpu *g, int x, int y, int w, int h, const uint16_t *src);
void psx_gpu_store_image(PsxGpu *g, int x, int y, int w, int h, uint16_t *dst);
void psx_gpu_fill(PsxGpu *g, int x, int y, int w, int h, uint8_t r, uint8_t gr, uint8_t b);

#endif
