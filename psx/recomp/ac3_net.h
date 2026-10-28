/* Etapa 10, paso 7: el juego en red por lockstep (docs/16-etapa10.md).
 *
 * Los dos aparatos ejecutan el juego desde el arranque con los mismos mandos: cada uno envia en cada VBlank solo el
 * suyo, para RETRASO VBlank mas adelante, y en cada VBlank aplica el de los dos (el anfitrion es el jugador 1, puerto
 * 1; el invitado, el 2, puerto 2) cuando los tiene; si aun no ha llegado el del otro, espera. Cada 60 VBlank se
 * cambian la huella de la RAM: si difiere, las partidas se han separado y se dice. El anfitrion escucha y el invitado
 * conecta (TCP). Solo codigo nuestro: sockets POSIX, igual en el PC y en Android. */
#ifndef AC3_NET_H
#define AC3_NET_H

#include <stdint.h>

typedef struct { uint16_t buttons; uint8_t lx, ly, rx, ry; } NetPad;   /* el mando de un VBlank (0x80: palanca al centro) */

#define AC3_NET_OPT_WIDE 1u   /* opciones que cambian la partida y deben ser iguales en los dos: la imagen a 16:9 */

/* conecta (anfitrion: escucha en `port`; invitado: conecta a ip:port, reintentando) y saluda: version, papeles,
 * retraso (manda el del anfitrion) y opciones. Espera hasta `timeout_s`. 0 si queda conectado */
int ac3_net_open(int host, const char *ip, int port, int delay, uint32_t opts, int timeout_s);
int ac3_net_active(void);
int ac3_net_is_host(void);
/* en la espera del VBlank v (antes de que el juego lea el mando): envia el mando local para v + retraso y devuelve en
 * p1 / p2 los de los dos jugadores para v (esperando el del otro si hace falta). Los primeros `retraso` VBlank, sin
 * mando. Si el otro calla demasiado, la red se da por perdida y el suyo queda suelto */
void ac3_net_frame(int v, const NetPad *local, NetPad *p1, NetPad *p2);
/* cada 60 VBlank: la huella de la RAM de v (se envia y se compara con la del otro) */
int ac3_net_hash_due(int v);
void ac3_net_hash(int v, uint64_t h);
/* la huella de la RAM en 64 bits: los 32 bloques, el scratchpad y la semilla de rand (como AC3_RAMHASH) */
uint64_t ac3_net_ram_hash(void);
/* cifras: VBlank comparados, distintos (el primero en *first), esperas y ms esperando */
void ac3_net_stats(int *compared, int *differ, int *first, int *waits, double *wait_ms);

#endif
