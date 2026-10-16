/* Mezclador de efectos (16 voces, 44,1 kHz estereo) y salida por AAudio en Android.
 * En el PC no hay salida: audio_mix() permite volcar la mezcla a un WAV para pruebas. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include "sound.h"

#define AUDIO_RATE 44100
#define AUDIO_VOICES 16

int audio_init(const Sound *slots);         /* 0 si hay salida de audio */
void audio_shutdown(void);
void audio_pause(int paused);
/* Inicia el sonido de la ranura `slot`; pitch en semitonos (+-), vol 0..1, pan -1..1.
 * Devuelve el numero de voz o -1. */
int audio_play(int slot, float vol, float pan, float pitch);
void audio_set(int voice, float vol, float pan, float pitch);
void audio_stop(int voice);
void audio_stop_all(void);
/* Mezcla `frames` muestras estereo (para el backend o para pruebas). */
void audio_mix(int16_t *out, int frames);

#endif
