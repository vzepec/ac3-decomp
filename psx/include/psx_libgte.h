/* Capa de compatibilidad PsyQ (etapa 5): libgte.
 * Misma interfaz que la libreria de Sony; ver docs/10-etapa5.md.
 * Validacion: cada funcion se compara con el codigo original de ACE.BIN
 * ejecutado en tools/mipsemu.py (tools/psyqcheck.py). */
#ifndef PSX_LIBGTE_H
#define PSX_LIBGTE_H

#include <stddef.h>
#include <stdint.h>

/* Tipos de PsyQ (mismo tamano y orden de campos). */
typedef struct { short m[3][3]; long t[3]; } MATRIX;
typedef struct { long vx, vy, vz, pad; } VECTOR;
typedef struct { short vx, vy, vz, pad; } SVECTOR;
typedef struct { unsigned char r, g, b, cd; } CVECTOR;

/* Tablas: la de senos y la de raices se generan por formula (exactas, validadas
 * con el codigo original); la de arcotangente no tiene formula conocida y se lee
 * del ACE.BIN del usuario. Devuelve 0 si todo bien, -1 si no encuentra la tabla
 * de arcotangente (entonces ratan2 usa una aproximacion y lo indica). */
int psx_gte_tables_init(const uint8_t *acebin, size_t len);
int psx_gte_ratan_exact(void);

int rsin(int a);
int rcos(int a);
long ratan2(long y, long x);
long SquareRoot0(long a);

#endif
