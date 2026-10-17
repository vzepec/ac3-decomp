/* Demo de la capa PsyQ (etapa 6) en la rebanada: lo que se ve sale ENTERO de psx/ (la
 * reimplementacion de las librerias de Sony) y de su GPU por software, no del render de la
 * rebanada:
 *   0 = pelicula de introduccion, 1 = pelicula final: libpress (DecDCTvlc2), MDEC, VRAM de
 *       24 bits (LoadImage), salida de video y audio XA, sincronizado con el audio;
 *   2 = terreno de la mision con libgte (RTPT, NCLIP...) y libgpu (POLY_FT4/GT4, tabla de
 *       ordenacion, DrawOTag) a 320x240, con la camara girando en el inicio de la mision.
 * La imagen se presenta con psx/gl/psx_present.c a 4:3. */
#ifndef PSXDEMO_H
#define PSXDEMO_H

#include <stdint.h>
#include "ace.h"

enum { PSXDEMO_INTRO = 0, PSXDEMO_ENDING = 1, PSXDEMO_TERRAIN = 2, PSXDEMO_COUNT = 3 };

/* empieza una demo; record = registro de la mision (terreno); x, y, z = inicio en
 * coordenadas PS1. 0 si va bien */
int psxdemo_start(Disc *d, AcePack *p, int which, uint32_t record, float x, float y, float z);
void psxdemo_stop(void);
/* imagen a mostrar ahora (RGBA); NULL si aun no hay ninguna */
const uint32_t *psxdemo_frame(int *w, int *h, float dt);

#endif
