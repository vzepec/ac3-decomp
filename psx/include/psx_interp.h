/* Interpolacion de imagenes (etapa 6, paso 5): mas imagenes por segundo que pasos de logica.
 *
 * El juego dibuja una imagen por paso de logica (25 por segundo en la mision PAL: bitacora
 * 2026-10-17). Para mostrar imagenes intermedias sin tocar la logica, la capa:
 *  1. anota cada vertice que proyecta la GTE (RTPS/RTPT): coordenadas del modelo -> posicion
 *     de pantalla (SXY). La clave del vertice es (x, y, z del modelo, n-esima aparicion);
 *  2. en DrawOTag recorre la lista de dibujo y reconoce las coordenadas de los poligonos que
 *     salieron de la GTE en este paso (por su valor exacto);
 *  3. busca la posicion del mismo vertice en el paso anterior y dibuja la lista con las
 *     posiciones mezcladas (VRAM y estado de la GPU se restauran despues). Los vertices sin
 *     pareja de un poligono con alguno emparejado se mueven con la media de los emparejados;
 *     los poligonos sin ninguno (HUD, textos) no se tocan. Saltos grandes (cortes de camara,
 *     parejas equivocadas) no se interpolan.
 * Las imagenes intermedias van a un sumidero del anfitrion con su fase t (0 < t < 1, entre el
 * paso anterior y este); la imagen real (t = 1) tambien. El anfitrion decide cuando mostrarlas. */
#ifndef PSX_INTERP_H
#define PSX_INTERP_H

#include <stdint.h>

/* t_step: hora (CLOCK_MONOTONIC, segundos) en que el juego entrego el paso. Con hilos de dibujo
 * (psx_interp_threads) el sumidero se llama desde esos hilos y las intermedias llegan despues
 * de la imagen real: el anfitrion las ordena por fase. */
typedef void (*PsxInterpSink)(const uint32_t *rgba, int w, int h, int phase_num, int phase_den, int vblanks, double t_step);

/* nfrac imagenes intermedias por paso (0 = desactivado); solo si el paso dura >= min_vblanks */
void psx_interp_config(int nfrac, int min_vblanks, PsxInterpSink sink);
/* gancho de la GTE (psx_gte_vertex_hook) tras proyectar un vertice */
void psx_interp_vertex(int32_t x, int32_t y, int32_t z, int32_t sx, int32_t sy);
/* llamado por DrawOTag: dibuja (intermedias + real) y devuelve 1, o 0 si no actua */
int psx_interp_draw(void *gpu, uint32_t ot_addr);
/* estadisticas del ultimo paso: vertices de la GTE, coordenadas de la lista reconocidas,
 * emparejadas con el paso anterior, movidas por la media */
void psx_interp_stats(int *gte, int *matched, int *paired, int *dragged, int *total);

/* n hilos de dibujo para las intermedias (0 = en el hilo del juego, dentro de DrawOTag, como
 * en la prueba de native_main). on_start: se llama al arrancar cada hilo (afinidad...) */
void psx_interp_threads(int n, void (*on_start)(void));

/* rendimiento acumulado desde el ultimo reset: pasos interpolados, trabajos entregados a los
 * hilos, pasos sin intermedias porque los hilos no habian terminado; ms por paso en el hilo del
 * juego (emparejar, instantanea, dibujo real) y ms por trabajo en cada hilo de dibujo */
typedef struct {
    int steps, jobs, skipped, nfrac, threads;
    double pair_ms, snap_ms, real_ms, worker_ms, worker_max_ms;
} PsxInterpPerf;
void psx_interp_perf(PsxInterpPerf *p, int reset);

extern int psx_interp_on;
/* prueba (tools: AC3_INTERP_TEST de native_main): un paso de cada dos se entrega con fase 0/0
 * y no sirve de referencia, para comparar la intermedia de k y k+2 con el k+1 real */
extern int psx_interp_skip_test;   /* atajo para la GTE */

#endif
