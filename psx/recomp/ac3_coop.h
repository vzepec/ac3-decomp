/* Etapa 9, paso 3: el segundo avion por cambio de contexto (docs/15-etapa9.md).
 *
 * El juego es de un jugador, pero todo lo propio del jugador cuelga de pocos sitios: la entidad 0
 * de la tabla de entidades, seis bloques pequenos (punteros en DAT_800BBA68..80, los reserva
 * FUN_8006b7b4) y la entrada del juego (g_state + 0x120). La ranura 1 de avion esta libre en las 41
 * misiones y el juego ya lee el puerto 2 (su ranura de mando 4).
 *
 * En cada paso de vuelo, al terminar el jugador 1 (entrada de la funcion que le sigue), se activa
 * la entidad 1 (copia de la 0 la primera vez), se le reservan sus seis bloques con el asignador del
 * juego, se traduce el mando del puerto 2 con la funcion del juego, se cambia el contexto (entidad
 * 0 <-> 1, los seis punteros, la entrada) y se vuelve a llamar al paso del jugador. Despues, todo
 * vuelve a ser del jugador 1. Solo codigo nuestro: direcciones y llamadas al codigo del disco.
 *
 * (paso 5) Su vista: su camara, su blanco y su fijado y sus registros de pantalla son suyos (se intercambian con su
 * contexto); en la simulacion, en los dos aparatos, se hacen para el su paso de camara y la logica de su HUD (sin ellos
 * sus misiles salen sin blanco). Y la imagen desde su avion (ac3_coop_draw_p2) se redibuja en DrawOTag sin tocar la
 * partida: cada aparato puede dibujar a su jugador con la misma partida. */
#ifndef AC3_COOP_H
#define AC3_COOP_H

#include "ac3_versions.h"

/* 0 si esta version tiene direcciones; engancha el paso */
int ac3_coop_install(const Ac3Version *v);
/* medicion: pasos del jugador 2 y la entidad 1 (0 si no esta activa) */
int ac3_coop_steps(void);
int ac3_coop_orders(void);   /* ordenes de modo ejecutadas tambien para el jugador 2 */
int ac3_coop_revives(void);  /* reapariciones del jugador 2 */
uint32_t ac3_coop_entity(void);
/* (paso 5) en DrawOTag (ot: la lista del paso): reconstruye la lista desde el avion del jugador 2, la pasa a draw y deja
 * la RAM, el scratchpad y la GTE como estaban. -1 (sin hacer nada) si en este VBlank no hubo paso de vuelo con el 2 */
int ac3_coop_draw_p2(uint32_t ot, void (*draw)(uint32_t ot, void *u), void *u);

#endif
