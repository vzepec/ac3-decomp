/* Capa de compatibilidad PsyQ (etapa 5): libspu sobre la SPU por software (psx_spu.h).
 * Estructuras con la disposicion de PsyQ 4.x.
 *
 * Funciones de registros (SpuSetVoiceAttr, SpuSetCommonAttr, SpuSetKey,
 * SpuGetAllKeysStatus, _spu_note2pitch, SpuSetReverb, SpuSetReverbDepth,
 * SpuSetReverbVoice): reproducen las escrituras de la libreria original del ACE.BIN
 * europeo y se comparan con ella en tools/psyqcheck_spu.py.
 * Transferencias (SpuWrite, SpuWrite0, SpuRead): inmediatas (en la original terminan con
 * la interrupcion de DMA y un evento de la BIOS); SpuIsTransferCompleted devuelve 1.
 * SpuSetReverbModeParam y SpuClearReverbWorkArea guardan el modo pero no reproducen las
 * escrituras de la original (la SPU por software no procesa reverberacion: SIN HACER).
 * Nombres: en el ACE.BIN las firmas atribuyen mal tres funciones; aqui se usan los
 * nombres reales: SpuInit (firmada como SsUtReverbOff), SpuWrite (firmada como SpuRead),
 * SpuQuit y SpuSetReverbVoice (sin firma, 0x8009E238 y 0x8009EEC8). */
#ifndef PSX_LIBSPU_H
#define PSX_LIBSPU_H

#include <stdint.h>

#include "psx_spu.h"

typedef struct { int16_t left, right; } SpuVolume;

typedef struct {
    uint32_t voice;         /* 0 */
    uint32_t mask;          /* 4 */
    SpuVolume volume;       /* 8 */
    SpuVolume volmode;      /* 12 */
    SpuVolume volumex;      /* 16 */
    uint16_t pitch;         /* 20 */
    uint16_t note;          /* 22 */
    uint16_t sample_note;   /* 24 */
    int16_t envx;           /* 26 */
    uint32_t addr;          /* 28 */
    uint32_t loop_addr;     /* 32 */
    int32_t a_mode, s_mode, r_mode;   /* 36 40 44 */
    uint16_t ar, dr, sr, rr, sl;      /* 48 50 52 54 56 */
    uint16_t adsr1, adsr2;            /* 58 60 */
} SpuVoiceAttr;             /* 64 bytes */

typedef struct { SpuVolume volume; int32_t reverb, mix; } SpuExtAttr;

typedef struct {
    uint32_t mask;          /* 0 */
    SpuVolume mvol;         /* 4 */
    SpuVolume mvolmode;     /* 8 */
    SpuVolume mvolx;        /* 12 */
    SpuExtAttr cd;          /* 16 */
    SpuExtAttr ext;         /* 28 */
} SpuCommonAttr;            /* 40 bytes */

typedef struct {
    uint32_t mask;
    int32_t mode;
    SpuVolume depth;
    int32_t delay, feedback;
} SpuReverbAttr;            /* 20 bytes */

/* la SPU que usa la libreria (para el mezclador del anfitrion y las pruebas) */
PsxSpu *psx_spu(void);

void SpuInit(void);
void SpuStart(void);
void SpuQuit(void);
void SpuSetCommonAttr(const SpuCommonAttr *attr);
void SpuSetVoiceAttr(const SpuVoiceAttr *arg);
void SpuSetKey(int32_t on_off, uint32_t voice_bit);
void SpuGetAllKeysStatus(char *status);
uint16_t _spu_note2pitch(int cen_hi, int cen_lo, int note_hi, int note_lo);
int32_t SpuSetTransferMode(int32_t mode);
uint32_t SpuSetTransferStartAddr(uint32_t addr);
uint32_t SpuWrite(const uint8_t *addr, uint32_t size);
uint32_t SpuWrite0(uint32_t size);
uint32_t SpuRead(uint8_t *addr, uint32_t size);
int32_t SpuIsTransferCompleted(int32_t flag);
int32_t SpuSetReverb(int32_t on_off);
int32_t SpuSetReverbDepth(const SpuReverbAttr *attr);
uint32_t SpuSetReverbVoice(int32_t on_off, uint32_t voice_bit);
int32_t SpuSetReverbModeParam(const SpuReverbAttr *attr);
int32_t SpuClearReverbWorkArea(int32_t mode);

#endif
