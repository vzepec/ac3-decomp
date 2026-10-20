/* Dibujo por GLES a alta resolucion (etapa 6, paso 7).
 *
 * Renderizador hibrido: la GPU por software (psx/src/gpu.c) sigue dibujando a resolucion
 * original y su VRAM es la buena (la que el juego lee). Al dibujar una imagen, la GPU anota sus
 * primitivas (psx_gpu_record); este modulo las vuelve a dibujar con GLES en un framebuffer de
 * escala N, tomando las texturas de la VRAM (subida como textura entera de 16 bits) con un shader
 * que hace lo mismo que la GPU por software: CLUT de 4/8 bits, 15 bits directos, ventana de
 * textura, modulacion color * texel / 128, texel 0 transparente, semitransparencia por primitiva
 * (y por texel con el bit 15) con mezcla de GL. Sin tramado ni bit de mascara (a alta resolucion
 * no aportan). El fondo de la imagen es la VRAM de antes de dibujar la lista, ampliada.
 * Requiere un contexto de OpenGL ES 3.0 activo. */
#ifndef PSX_HW_H
#define PSX_HW_H

#include <stdint.h>

#include "psx_gpu.h"

int psx_hw_init(void);
void psx_hw_shutdown(void);
/* dibuja la imagen del area (x0, y0, w, h) de la VRAM a escala `scale` desde la lista anotada
 * (sus UPLOAD actualizan la textura a medida que avanza) y bg, la VRAM de antes de la lista
 * (fondo y texturas de partida). El resultado queda en la textura RGBA de psx_hw_texture (fila 0
 * = arriba), de w*scale x h*scale */
void psx_hw_frame(const PsxHwList *l, const uint16_t *bg, int x0, int y0, int w, int h, int scale);
/* etapa 8, paso 2: lo mismo conservando la imagen en la GPU (una por zona de la VRAM: el doble
 * bufer). real: la imagen se dibuja sobre la conservada y se queda; si no (intermedia), sobre una
 * copia. Con l->main_stale se parte de la imagen conservada en lugar de la de bg (el software ya
 * no la tiene al dia) */
void psx_hw_frame_ex(const PsxHwList *l, const uint16_t *bg, int x0, int y0, int w, int h, int scale, int real);
unsigned psx_hw_texture(int *w, int *h);
/* tramado de la PS1 (matriz 4x4 por pixel de la VRAM); para comparar a escala 1 con el software */
void psx_hw_dither(int on);
/* lee el resultado (RGBA, fila 0 = arriba) en out; para las pruebas */
void psx_hw_read(uint32_t *out);
int psx_hw_read_mask(uint8_t *out);   /* pruebas: bit 15 de la imagen conservada de la ultima lista */

#endif
