/* Capa de compatibilidad PsyQ (etapa 5): libpad en modo directo (PadInitDirect), que es
 * el que usa el juego. Cada fotograma (psx_vblank) la libreria escribe en los buferes
 * del juego lo que recibiria del mando por el puerto serie:
 *   [0] estado (0x00 = hay mando, 0xFF = no), [1] id (0x41 digital, 0x73 analogico),
 *   [2..3] botones activos a nivel bajo, [4..7] analogicos RX, RY, LX, LY.
 * El mando se presenta como un mando estandar ya identificado (PadGetState = 2,
 * PadStateFindCTP1): sin negociacion de DualShock ni vibracion (SIN HACER).
 * Orden de botones (bit 0..15): Select L3 R3 Start Arriba Derecha Abajo Izquierda
 * L2 R2 L1 R1 Triangulo Circulo Cruz Cuadrado. */
#ifndef PSX_LIBPAD_H
#define PSX_LIBPAD_H

#include <stdint.h>

enum { PSX_PAD_SELECT = 1 << 0, PSX_PAD_L3 = 1 << 1, PSX_PAD_R3 = 1 << 2, PSX_PAD_START = 1 << 3,
       PSX_PAD_UP = 1 << 4, PSX_PAD_RIGHT = 1 << 5, PSX_PAD_DOWN = 1 << 6, PSX_PAD_LEFT = 1 << 7,
       PSX_PAD_L2 = 1 << 8, PSX_PAD_R2 = 1 << 9, PSX_PAD_L1 = 1 << 10, PSX_PAD_R1 = 1 << 11,
       PSX_PAD_TRIANGLE = 1 << 12, PSX_PAD_CIRCLE = 1 << 13, PSX_PAD_CROSS = 1 << 14, PSX_PAD_SQUARE = 1 << 15 };

/* ---- anfitrion ---- */
/* puerto 0 o 1; id 0 = desconectado, 0x41 digital, 0x73 analogico */
void psx_pad_connect(int port, int id);
/* botones activos a nivel ALTO (PSX_PAD_*) y ejes de 0 a 255 (centro 0x80) */
void psx_pad_set(int port, uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);

/* ---- API de libpad que usa el juego ---- */
void PadInitDirect(uint8_t *pad1, uint8_t *pad2);
void PadStartCom(void);
void PadStopCom(void);
int PadEnableCom(int mode);
int PadChkVsync(void);
int PadGetState(int port);
int PadInfoMode(int port, int term, int offs);
int PadInfoAct(int port, int actno, int term);
int PadSetMainMode(int port, int offs, int lock);
void PadSetAct(int port, uint8_t *data, int len);
int PadSetActAlign(int port, const uint8_t *data);

#endif
