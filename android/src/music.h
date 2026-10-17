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
/* entrada `entry` del SPH: primer sector (LBA), extension y canal XA */
int sph_entry(Disc *d, int entry, uint32_t *start, uint32_t *count, uint8_t *chan);
/* modo de empuje (peliculas): para la pista y vacia el bufer; music_push mete muestras
 * estereo a 44100 Hz (0 si no caben); contadores de muestras metidas y ya mezcladas */
void music_stream_begin(void);
int music_push(const int16_t *stereo, int frames);
uint32_t music_pushed(void);
uint32_t music_played(void);
/* suma `frames` muestras estereo de musica (volumen 0..1) en acc (float, intercalado) */
void music_mix(float *acc, int frames, float vol);

#endif
