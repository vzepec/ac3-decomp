/* Capa de compatibilidad PsyQ (etapa 5): libgte.
 * Misma interfaz que la libreria de Sony; ver docs/10-etapa5.md.
 * Validacion: cada funcion se compara con el codigo original de ACE.BIN
 * ejecutado en tools/mipsemu.py (tools/psyqcheck.py). */
#ifndef PSX_LIBGTE_H
#define PSX_LIBGTE_H

#include <stddef.h>
#include <stdint.h>

/* Tipos de PsyQ con la disposicion de la PS1: `long` de la PS1 es de 32 bits, asi que
 * se usa int32_t (en un sistema de 64 bits `long` mediria 8 bytes). MATRIX = 32 B. */
typedef struct { int16_t m[3][3]; int32_t t[3]; } MATRIX;
typedef struct { int32_t vx, vy, vz, pad; } VECTOR;
typedef struct { int16_t vx, vy, vz, pad; } SVECTOR;
typedef struct { uint8_t r, g, b, cd; } CVECTOR;

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

/* ---- funciones que usan la GTE (psx/src/libgte.c) ----
 * Trabajan sobre el contexto de GTE activo (psx_gte_set_context). */
struct PsxGteCtx;
void psx_gte_set_context(void *gte);       /* PsxGte * (psx_gte.h) */
void *psx_gte_get_context(void);

void InitGeom(void);
void ReadGeomOffset(int32_t *ofx, int32_t *ofy);
void SetGeomOffset(int32_t ofx, int32_t ofy);
void SetGeomScreen(int32_t h);
int32_t ReadGeomScreen(void);
int32_t NormalClip(int32_t sxy0, int32_t sxy1, int32_t sxy2);
MATRIX *TransMatrix(MATRIX *m, VECTOR *v);
MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2);
MATRIX *RotMatrix_gte(SVECTOR *r, MATRIX *m);
void SetRotMatrix(MATRIX *m);
void SetTransMatrix(MATRIX *m);
void SetLightMatrix(MATRIX *m);
void SetColorMatrix(MATRIX *m);
void SetBackColor(int32_t rbk, int32_t gbk, int32_t bbk);
int32_t RotTransPers(SVECTOR *v, int32_t *sxy, int32_t *p, int32_t *flag);
int32_t RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, int32_t *sxy0, int32_t *sxy1, int32_t *sxy2,
                      int32_t *p, int32_t *flag);
void RotTrans(SVECTOR *v, VECTOR *out, int32_t *flag);
MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2);
MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1);
MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1);
MATRIX *SetMulMatrix(MATRIX *m0, MATRIX *m1);
SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v, SVECTOR *out);
VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v, VECTOR *out);
VECTOR *ApplyRotMatrixLV(VECTOR *v, VECTOR *out);
MATRIX *TransposeMatrix(MATRIX *m0, MATRIX *m1);
MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v);
void OuterProduct0(VECTOR *a, VECTOR *b, VECTOR *out);
MATRIX *RotMatrix(SVECTOR *r, MATRIX *m);
MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m);
int32_t VectorNormalSS(SVECTOR *v0, SVECTOR *v1);

#endif
