/* Audio XA-ADPCM de los sectores de CD (etapa 5), segun psx-spx: 18 grupos de 128 bytes
 * por sector, 4 u 8 bits por muestra, mono o estereo, 37800 o 18900 Hz. Validado contra
 * el decodificador adpcm_xa de libav/FFmpeg (tools/psxcheck_xa.py).
 * La consola remuestrea a 44100 Hz con un filtro de 7 puntos; aqui se usa interpolacion
 * lineal (SIN HACER: el filtro de la consola). */
#ifndef PSX_XA_H
#define PSX_XA_H

#include <stdint.h>

typedef struct {
    int32_t s1[2], s2[2];   /* historia del filtro por canal */
    /* remuestreo a 44100 */
    uint32_t pos;           /* fase en 1/65536 de muestra de entrada */
    int16_t last[2];        /* ultima muestra de entrada (interpolacion entre sectores) */
} PsxXa;

void psx_xa_reset(PsxXa *x);
/* decodifica un sector crudo (2352 bytes) a su frecuencia nativa; devuelve el numero de
 * muestras por canal (0 si no es audio) y rellena `out` (intercalado si es estereo),
 * `stereo` y `rate`. `out` debe admitir 4032 muestras. */
int psx_xa_decode(PsxXa *x, const uint8_t *raw, int16_t *out, int *stereo, int *rate);
/* decodifica y remuestrea a 44100 Hz estereo intercalado; devuelve las muestras estereo */
int psx_xa_decode44(PsxXa *x, const uint8_t *raw, int16_t *out, int max_frames);
/* sumidero para libcd: decodifica y empuja a la entrada de CD de la SPU de libspu */
void psx_xa_to_spu(const uint8_t *raw);
/* conecta libcd -> XA -> SPU */
void psx_audio_connect(void);

#endif
