/* Capa de compatibilidad PsyQ (etapa 5): libetc (VSync y callbacks de interrupcion) y el
 * modelo de tiempo e interrupciones de la capa.
 *
 * Modelo: no hay hilos ni interrupciones reales. El tiempo avanza por fotogramas
 * (psx_vblank): cada uno incrementa el contador de VSync, llama al callback de VSync,
 * actualiza los mandos (libpad) y hace avanzar el CD (libcd: comandos pendientes y
 * sectores a la velocidad de lectura). Las funciones que en la PS1 esperan (VSync,
 * CdSync(0), CdControlB...) hacen avanzar ese tiempo ellas mismas, de modo que los
 * callbacks se entregan "durante la espera", como harian las interrupciones.
 *
 * Callbacks: el juego guarda punteros a funcion en sus estructuras como palabras de 32
 * bits (p. ej. 0x80029BB0). La capa los trata como DIRECCIONES PS1 y los invoca a traves
 * de una funcion de despacho que pone el anfitrion (psx_set_call): el codigo recompilado
 * (etapa 6) traducira la direccion a su funcion; las pruebas la traducen al interprete
 * MIPS (tools/psxcheck_io.py). */
#ifndef PSX_LIBETC_H
#define PSX_LIBETC_H

#include <stdint.h>

typedef void (*PsxCallFn)(uint32_t addr, uint32_t a0, uint32_t a1);
void psx_set_call(PsxCallFn fn);
void psx_call(uint32_t addr, uint32_t a0, uint32_t a1);

/* espera real del anfitrion antes de cada fotograma (ritmo de 50/60 Hz); sin ella, el
 * tiempo simulado avanza tan rapido como se le pida */
void psx_set_frame_wait(void (*fn)(void));
/* un fotograma: interrupcion de VBlank y todo lo que cuelga de ella */
void psx_vblank(void);
/* frecuencia de fotogramas: 50 (PAL, por defecto como el disco europeo) o 60 */
void psx_set_refresh(int hz);
int psx_refresh(void);

/* ---- API de libetc que usa el juego ---- */
int VSync(int mode);
int VSyncCallback(uint32_t func);
int ResetCallback(void);
int StopCallback(void);
int CheckCallback(void);
int DMACallback(int dma, uint32_t func);
int SetIntrMask(int mask);

#endif
