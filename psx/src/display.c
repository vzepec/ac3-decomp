/* Salida de video: VRAM visible -> RGBA (ver psx_display.h). */
#include <string.h>

#include "psx_display.h"
#include "psx_gpu.h"
#include "psx_libgpu.h"

void *psx_gpu(void);   /* libgpu.c */

static uint32_t rgba(int r, int g, int b) { return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | 0xFF000000u; }
static int x5(int c) { return (c << 3) | (c >> 2); }

int psx_display_frame(uint32_t *out, int max_pixels, int *w, int *h) { return psx_display_frame_at(-1, -1, out, max_pixels, w, h); }

int psx_display_frame_at(int ox, int oy, uint32_t *out, int max_pixels, int *w, int *h) {
    int x0, y0, dw, dh, on, rgb24, inter;
    psx_gpu_display(&x0, &y0, &dw, &dh, &on);
    if (ox >= 0) { x0 = ox; y0 = oy; }
    psx_gpu_display_mode(&rgb24, &inter);
    if (dw <= 0 || dh <= 0 || dw * dh > max_pixels) { *w = *h = 0; return 0; }
    *w = dw;
    *h = dh;
    return psx_display_convert(((PsxGpu *)psx_gpu())->vram, x0, y0, dw, dh, on, rgb24, out);
}

void psx_display_geom(int *w, int *h, int *on, int *rgb24) {
    int x0, y0, inter;
    psx_gpu_display(&x0, &y0, w, h, on);
    psx_gpu_display_mode(rgb24, &inter);
}

int psx_display_convert(const uint16_t *vram, int x0, int y0, int dw, int dh, int on, int rgb24, uint32_t *out) {
    if (!on) { for (int i = 0; i < dw * dh; i++) out[i] = 0xFF000000u; return 0; }
    for (int y = 0; y < dh; y++) {
        const uint16_t *row = vram + ((y0 + y) & (PSX_VRAM_H - 1)) * PSX_VRAM_W;
        uint32_t *o = out + y * dw;
        if (!rgb24) {
            for (int x = 0; x < dw; x++) {
                uint16_t c = row[(x0 + x) & (PSX_VRAM_W - 1)];
                o[x] = rgba(x5(c & 31), x5((c >> 5) & 31), x5((c >> 10) & 31));
            }
        } else {
            for (int x = 0; x < dw; x++) {
                uint8_t b3[3];
                for (int k = 0; k < 3; k++) {
                    int i = 3 * x + k;   /* byte i de la fila, desde la media palabra x0 */
                    uint16_t hw = row[(x0 + i / 2) & (PSX_VRAM_W - 1)];
                    b3[k] = (uint8_t)(i & 1 ? hw >> 8 : hw);
                }
                o[x] = rgba(b3[0], b3[1], b3[2]);
            }
        }
    }
    return 1;
}
