/* Mando virtual en pantalla (etapa 7, paso 8).
 *
 * Capa semitransparente dibujada con GLES encima de la imagen del juego: stick analogico flotante
 * (el centro se pone donde se apoya el pulgar), cruceta, botones de cara, L1/L2/R1/R2,
 * START/SELECT y un boton para ocultarla. Multitoque: cada dedo se queda con el control que toco
 * (los botones de cara y la cruceta dejan deslizar el dedo de uno a otro). Los graficos los dibuja
 * MainActivity (Java, Canvas) en un atlas al arrancar: solo dibujo propio, nada del juego.
 *
 * Modo: 0 automatico (se oculta al usar un mando fisico y vuelve al tocar la pantalla), 1 siempre,
 * 2 nunca. */
#ifndef TOUCHPAD_H
#define TOUCHPAD_H

#include <android/input.h>
#include <stdint.h>

/* celdas del atlas (las mismas en MainActivity.touchAtlas) */
enum { TP_CROSS, TP_CIRCLE, TP_SQUARE, TP_TRIANGLE, TP_L1, TP_L2, TP_R1, TP_R2, TP_START, TP_SELECT,
       TP_UP, TP_DOWN, TP_LEFT, TP_RIGHT, TP_BASE, TP_KNOB, TP_HIDE, TP_NCELL };
#define TP_ATLAS_COLS 5
#define TP_CELL 128

void touchpad_set_mode(int mode);
void touchpad_set_atlas(const uint32_t *argb, int w, int h);   /* hilo de Java: se copia */
/* densidad de la pantalla (dpi, AConfiguration_getDensity) y tamano en pixeles */
void touchpad_set_screen(int w, int h, int dpi);
/* un evento de la pantalla tactil: 1 si lo consume */
int touchpad_event(const AInputEvent *e);
/* llega algo de un mando fisico (en modo automatico, se oculta) */
void touchpad_physical(void);
/* lo que pulsa ahora: botones (PSX_PAD_*) y stick (-1..1; *active = 0 si el stick no se toca) */
uint16_t touchpad_buttons(void);
void touchpad_stick(float *x, float *y, int *active);
/* dibuja la capa (contexto GLES activo, viewport de toda la ventana) */
void touchpad_draw(void);
void touchpad_gl_lost(void);   /* el contexto se perdio: rehacer texturas y programa */
/* (etapa 7, paso 9) un rotulo centrado en pantalla (el de la pausa al volver a la app): lo dibuja
 * MainActivity en el idioma del menu */
void touchpad_set_label(const uint32_t *argb, int w, int h);
void touchpad_draw_label(void);

#endif
