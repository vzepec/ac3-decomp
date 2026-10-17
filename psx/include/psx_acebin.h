/* ACE.BIN desde el disco del usuario (etapa 7, paso 3): el modulo principal del juego va
 * comprimido en el disco con el LZ propio de Namco y una cabecera "PS-Y" (formato resuelto en la
 * etapa 2: tools/acebin.py, docs/02-binarios.md). El APK distribuible no lo lleva: lo lee de la
 * imagen elegida por el usuario y lo descomprime al arrancar, con las mismas comprobaciones que
 * el cargador del juego (suma u32 del archivo = 0, tamano exacto, suma de la salida = campo 0x58). */
#ifndef PSX_ACEBIN_H
#define PSX_ACEBIN_H

#include <stdint.h>

typedef struct { uint32_t size, load, entry; } PsxAceBin;

enum { PSX_ACEBIN_OK = 0, PSX_ACEBIN_NOFILE = -1, PSX_ACEBIN_MAGIC = -2, PSX_ACEBIN_HEADER = -3, PSX_ACEBIN_SUM = -4,
       PSX_ACEBIN_STREAM = -5, PSX_ACEBIN_OUTSUM = -6 };

/* descomprime ACE.BIN (raw: el archivo tal cual) en out (hasta max bytes) */
int psx_acebin_unpack(const uint8_t *raw, uint32_t raw_len, uint8_t *out, uint32_t max, PsxAceBin *info);
/* lo busca en el disco abierto (psx_cd_open) y lo descomprime */
int psx_acebin_from_disc(uint8_t *out, uint32_t max, PsxAceBin *info);

#endif
