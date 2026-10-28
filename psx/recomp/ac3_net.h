/* Etapa 10, pasos 7-8: el juego en red por lockstep (docs/16-etapa10.md).
 *
 * Los dos aparatos ejecutan el juego desde el arranque con los mismos mandos: cada uno envia en cada VBlank solo el
 * suyo, para RETRASO VBlank mas adelante, y en cada VBlank aplica el de los dos (el anfitrion es el jugador 1, puerto
 * 1; el invitado, el 2, puerto 2) cuando los tiene; si aun no ha llegado el del otro, espera. Cada 60 VBlank se
 * cambian la huella de la RAM: si difiere, las partidas se han separado y se dice. El anfitrion escucha y el invitado
 * conecta (TCP). Solo codigo nuestro: sockets POSIX, igual en el PC y en Android. */
#ifndef AC3_NET_H
#define AC3_NET_H

#include <stddef.h>
#include <stdint.h>

typedef struct { uint16_t buttons; uint8_t lx, ly, rx, ry; } NetPad;   /* el mando de un VBlank (0x80: palanca al centro) */

#define AC3_NET_OPT_WIDE 1u   /* opciones que cambian la partida y deben ser iguales en los dos: la imagen a 16:9 */
#define AC3_NET_OPT_COOP 2u   /* (paso 8) y el cooperativo (el segundo avion; sin el, en el paso 8, se separaban en el VBlank 60) */
#define AC3_NET_OPT_MENUS 4u  /* (paso 8) los menus sin guion: la tarjeta de quien crea en los dos y la mision en la partida
                               * nueva */

/* (paso 8) lo que cada uno dice de si en el saludo (y el anfitrion en su anuncio): las opciones, el disco (huella de
 * ACE.BIN: deben ser iguales), la mision que eligio el anfitrion (-1: ninguna, se va por los menus del juego), el
 * nombre del aparato y el de la mision, para la lista de partidas */
typedef struct {
    uint32_t opts, disc;
    int32_t mission;
    char name[32], label[48];
} NetInfo;

/* conecta (anfitrion: escucha en `port` y se anuncia por UDP en el mismo puerto cada segundo; invitado: conecta a
 * ip:port, reintentando) y saluda: version, papeles, retraso (manda el del anfitrion) y NetInfo (en theirs, lo del
 * otro). Espera hasta `timeout_s`. 0 si queda conectado; si no, ac3_net_error() dice por que */
int ac3_net_open(int host, const char *ip, int port, int delay, const NetInfo *mine, NetInfo *theirs, int timeout_s);
const char *ac3_net_error(void);
/* (paso 8) justo tras conectar, antes del primer VBlank: el anfitrion envia n bytes (su tarjeta de memoria) y el
 * invitado los recibe. 0 si bien */
int ac3_net_send_blob(const void *p, size_t n);
int ac3_net_recv_blob(void *p, size_t n, int timeout_s);
/* la huella de ACE.BIN (FNV-1a de 32 bits): el mismo disco en los dos */
uint32_t ac3_net_disc_id(const uint8_t *ace, size_t n);
/* las IPv4 de este aparato (sin la de bucle), separadas por comas, para decirlas en pantalla */
void ac3_net_local_ips(char *out, size_t n);
int ac3_net_active(void);
int ac3_net_lost(void);   /* (paso 8) conecto y despues perdio al otro (para avisar) */
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
/* (paso 8) el ping: ida y vuelta de un mensaje (tipo 3, devuelto como tipo 4) cada 30 VBlank, en ms (media de los
 * ultimos 8; incluye hasta un VBlank del otro, que lo devuelve al leer la red en su espera). -1 si aun no hay */
double ac3_net_ping_ms(void);
/* cifras: VBlank comparados, distintos (el primero en *first), esperas y ms esperando */
void ac3_net_stats(int *compared, int *differ, int *first, int *waits, double *wait_ms);

#endif
