/* SPU de la PlayStation por software (etapa 5): registros 0x1F801C00-0x1F801DFF, RAM de
 * sonido de 512 KB, 24 voces ADPCM con envolvente ADSR y salida estereo a 44100 Hz.
 * Segun psx-spx (nocash).
 *
 * Hecho: decodificacion ADPCM (filtros 0-4, banderas de fin/repeticion/inicio de bucle,
 * ENDX), tono (contador de 12 bits de fraccion, maximo 0x4000), ADSR (ataque, caida,
 * sostenido y liberacion lineales o exponenciales, ENVX), KON/KOFF, volumen fijo por voz
 * y principal, entrada de audio de CD (SPUCNT bit 0, volumen de CD), transferencias por
 * el puerto de datos y por DMA.
 * Simplificaciones (SIN HACER): interpolacion lineal en vez de la gaussiana de la
 * consola (con un retardo de una muestra), volumen en modo barrido (se usa el valor
 * fijo), ruido, modulacion de tono, reverberacion (los registros se guardan pero no se
 * procesa) e interrupcion de la SPU. La envolvente sigue la descripcion de psx-spx; no se
 * ha verificado contra una consola. */
#ifndef PSX_SPU_H
#define PSX_SPU_H

#include <stddef.h>
#include <stdint.h>

#define PSX_SPU_RAM (512 * 1024)
#define PSX_SPU_CD_BUF 8192   /* muestras estereo de la entrada de CD en cola */

typedef struct {
    uint32_t addr;          /* direccion del siguiente bloque ADPCM (bytes) */
    uint32_t repeat;        /* direccion de repeticion */
    int ignore_loop;        /* el programa escribio la direccion de repeticion */
    uint32_t counter;       /* posicion: muestra = counter >> 12 */
    int16_t buf[28];        /* bloque decodificado */
    int16_t s1, s2;         /* historia del filtro */
    int16_t prev;           /* muestra anterior (interpolacion) */
    uint8_t flags;          /* banderas del bloque actual */
    int phase;              /* 0 ataque, 1 caida, 2 sostenido, 3 liberacion, 4 apagada */
    int32_t level;          /* ENVX, 0..0x7FFF */
    int32_t wait;           /* muestras hasta el siguiente paso de la envolvente */
} PsxSpuVoice;

typedef struct {
    uint16_t regs[0x200];   /* 0x1F801C00 + 2*i (hasta 0x1F802000) */
    uint8_t ram[PSX_SPU_RAM];
    PsxSpuVoice v[24];
    uint32_t endx;
    uint32_t xfer;          /* direccion de transferencia en curso (bytes) */
    int16_t cd[PSX_SPU_CD_BUF][2];
    uint32_t cd_r, cd_w;
    uint32_t samples;       /* muestras generadas (contador para pruebas) */
    uint32_t cap;           /* posicion de captura (0..0x1FF) en los 4 KB iniciales de la RAM */
} PsxSpu;

void psx_spu_reset(PsxSpu *s);
/* acceso a los registros por desplazamiento desde 0x1F801C00 (0..0x3FE) */
void psx_spu_write16(PsxSpu *s, uint32_t off, uint16_t v);
uint16_t psx_spu_read16(PsxSpu *s, uint32_t off);
/* DMA del canal 4: `words` palabras de 32 bits entre la RAM y la SPU desde la direccion
 * de transferencia actual */
void psx_spu_dma_write(PsxSpu *s, const void *src, uint32_t words);
void psx_spu_dma_read(PsxSpu *s, void *dst, uint32_t words);
/* genera `frames` muestras estereo intercaladas a 44100 Hz */
void psx_spu_render(PsxSpu *s, int16_t *out, int frames);
/* entrada de audio de CD (XA) ya a 44100 Hz, estereo intercalado */
void psx_spu_cd_push(PsxSpu *s, const int16_t *stereo, int frames);
/* tamano de PsxSpu (para crear instancias desde ctypes) */
size_t psx_spu_sizeof(void);

#endif
