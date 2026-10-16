/* Bancos de sonido del SPU decodificados a PCM (docs/04-formatos.md, tools/acesnd.py).
 * La tabla del juego tiene ranuras: 0-75 banco comun (registro 0000 s3), 84-85 avion
 * (tipo 4 s2), 88-107 mision (tipo 17 s15). Sin dependencias de Android. */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#define SOUND_SLOTS 128

typedef struct {
    int16_t *pcm;
    uint32_t n;             /* muestras */
    uint32_t loop_start;    /* muestra de inicio del bucle (si loops) */
    int loops;              /* la muestra se repite (flag "repetir" del ADPCM) */
    float rate;             /* Hz: 44100 * 2^((nota - nota_base)/12) */
    float vol;              /* volumen de la entrada (0..1) */
} Sound;

/* Decodifica un banco (cabecera + entradas + ADPCM) en las ranuras b0..b0+n-1.
 * Devuelve el numero de sonidos cargados o -1 si el banco no es valido. */
int sound_load_bank(Sound *slots, const uint8_t *bank, uint32_t len);
void sound_free_slots(Sound *slots, int first, int last);

#endif
