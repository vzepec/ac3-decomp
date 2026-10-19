/* Etapa 8: imágenes intermedias redibujando el mundo estático con el código del propio juego
 * (docs/14-etapa8.md).
 *
 * En la misión, FUN_80033748 (EU) dibuja cada paso: limpia la tabla de orden, ejecuta la lógica
 * (que también dibuja los objetos móviles), construye las matrices de la cámara y dibuja el mundo
 * estático (terreno, cielo, objetos de escena: FUN_8003308c .. FUN_800331d4) y luego el HUD. Este
 * módulo guarda el estado al empezar ese dibujo estático (S0) y lo que escribe, y en DrawOTag puede
 * construir la lista de una imagen intermedia: quita de la cadena lo estático, vuelve a S0, pone
 * la cámara (y las otras entradas que cambian entre pasos) a medio camino entre el paso anterior y
 * este, vuelve a llamar a las funciones de dibujo del juego y recoloca el resultado en los cajones
 * de la tabla de orden. Con t = 1 reproduce la imagen original bit a bit (ac3_rr_selftest).
 *
 * Trabaja sobre psx_ram: quien lo llama guarda y restaura la RAM (y el scratchpad y la GTE) si
 * la partida debe seguir intacta. Usa el intérprete (mipsi) para los enganches y las llamadas. */
#ifndef AC3_RERENDER_H
#define AC3_RERENDER_H

#include <stdint.h>

#include "ac3_versions.h"

/* instala los enganches (inicio y fin del dibujo estático); 0 si la versión lo admite */
int ac3_rr_install(const Ac3Version *v);
/* hay un dibujo estático completo de este paso y el estado del anterior */
int ac3_rr_ready(void);
/* construye en psx_ram la lista de la imagen intermedia de fase t (0 = paso anterior, 1 = este)
 * a partir de la cadena que empieza en ot. Devuelve 0 si la construyó; -1 si no hay datos o si la
 * cámara salta (corte de plano: no se interpola) */
int ac3_rr_build(uint32_t ot, float t);
/* rango de memoria de las primitivas que dibujó el mundo estático en este paso */
void ac3_rr_static_range(uint32_t *a0, uint32_t *a1);

#endif
