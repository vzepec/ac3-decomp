/* Presentacion de la imagen de la capa con OpenGL ES 3 (etapa 6, paso 2): la imagen de
 * psx_display_frame se sube como textura y se dibuja en un cuadro. Va aparte de libpsx
 * para que la capa no dependa de OpenGL; la usan el PC (EGL) y Android.
 * Modos: PSX_FIT_43 (4:3 centrado con barras, como un televisor) y PSX_FIT_EXACT (1:1 en la
 * esquina, para las pruebas). Filtrado: vecino mas cercano (o lineal). */
#ifndef PSX_PRESENT_H
#define PSX_PRESENT_H

#include <stdint.h>

enum { PSX_FIT_43 = 0, PSX_FIT_EXACT = 1, PSX_FIT_INTEGER = 2, PSX_FIT_169 = 3 };   /* 16:9: pantalla ancha */

int psx_present_init(void);   /* 0 si va bien (contexto GLES 3 ya activo) */
void psx_present_frame(const uint32_t *rgba, int w, int h, int view_w, int view_h, int fit, int linear);
void psx_present_shutdown(void);

#endif
