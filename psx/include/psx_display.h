/* Salida de video de la capa (etapa 6, paso 2): el area visible de la VRAM (la del ultimo
 * PutDispEnv) convertida a RGBA de 8 bits por canal, lista para subirla como textura.
 *  - 15 bits: cada canal de 5 bits se expande a 8 como (c << 3) | (c >> 2) (convencion: la
 *    consola saca los 5 bits al conversor; la expansion solo fija como se representan).
 *  - 24 bits: la VRAM guarda R, G, B seguidos (3 bytes por pixel en medias palabras), como
 *    las peliculas que el MDEC decodifica en 24 bits.
 * Sin hacer: el recorte y la posicion de la imagen en el televisor (DISPENV.screen) y el
 * entrelazado (se muestran todas las lineas del area). */
#ifndef PSX_DISPLAY_H
#define PSX_DISPLAY_H

#include <stdint.h>

/* escribe en rgba (hasta max_pixels) la imagen visible; devuelve 1 si la pantalla esta
 * activa (SetDispMask) y 0 si no (imagen negra); ancho y alto en *w, *h */
int psx_display_frame(uint32_t *rgba, int max_pixels, int *w, int *h);

#endif
