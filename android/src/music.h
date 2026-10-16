/* Musica XA de las misiones: un hilo lee del disco del usuario los sectores de audio de la
 * pista (ACE.SPB, entrada del ACE.SPH), los decodifica con el decodificador validado de la
 * etapa 5 (psx/src/xa.c, identico a libav) y los deja en un bufer que suma el mezclador.
 * Solo con imagenes .bin de 2352 bytes por sector (las .iso no llevan el audio XA). */
#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>
#include "ace.h"

/* pista de la mision (registro 6..46) segun la tabla de demostraciones del juego
 * (tools/acemusic.py); -1 si no hay */
int music_track_for_mission(uint32_t record);
/* arranca la pista `entry` del SPH (se repite al acabar); 0 si va bien */
int music_start(Disc *d, int entry);
void music_stop(void);
/* suma `frames` muestras estereo de musica (volumen 0..1) en acc (float, intercalado) */
void music_mix(float *acc, int frames, float vol);

#endif
