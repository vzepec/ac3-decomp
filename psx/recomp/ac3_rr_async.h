/* Etapa 8: el redibujo del mundo estático en un hilo de dibujo (ver ac3_rerender.h y
 * ac3_rr_async.c). Solo en los binarios que enlazan la segunda copia (rrw_*) del intérprete. */
#ifndef AC3_RR_ASYNC_H
#define AC3_RR_ASYNC_H

#include "ac3_versions.h"
#include "psx_interp.h"

/* prepara la segunda copia para la versión v (de ac3_versions[]); 0 si la versión lo admite */
int ac3_rr_async_install(const Ac3Version *v);
/* para psx_interp_rebuild_async */
const PsxInterpRebuildAsync *ac3_rr_async(void);
/* medición de la copia: ms acumulados (S0, cadena, restaurar, dibujo del juego, cajones) y pasos */
void ac3_rr_async_times(double t[6]);

#endif
