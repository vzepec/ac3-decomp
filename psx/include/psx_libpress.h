/* Capa de compatibilidad PsyQ (etapa 6, paso 1): libpress (video) y el MDEC.
 * DecDCTvlc2: flujo de bits de un fotograma (version 2 o 3, la de Namco) -> codigos
 * run-level para el MDEC: palabra de cabecera 0x3800nnnn y, por bloque, el DC
 * (escala << 10 | valor) y los AC (run << 10 | nivel) terminados en 0xFE00.
 * La tabla de DecDCTvlcBuild no se genera: el decodificador usa la suya. */
#ifndef PSX_LIBPRESS_H
#define PSX_LIBPRESS_H

#include <stdint.h>

void DecDCTvlcBuild(uint16_t *table);
int32_t DecDCTvlcSize2(int32_t size);
int DecDCTvlc2(uint32_t *bs, uint32_t *buf, uint16_t *table);

/* MDEC (detalles en libpress.c). Sin firma en el ACE.BIN: DecDCTReset 0x800AACD8,
 * DecDCTin 0x800AAE30, DecDCTout 0x800AAEAC, DecDCToutSync 0x800AAF08,
 * DecDCToutCallback 0x800AAF74 (identificadas por su codigo). */
void DecDCTReset(int mode);
void DecDCTin(uint32_t *buf, int mode);
void DecDCTout(uint32_t *buf, int size);
int DecDCTinSync(int mode);
int DecDCToutSync(int mode);
uint32_t DecDCToutCallback(uint32_t func);   /* direccion PS1 (psx_libetc.h) */

#endif
