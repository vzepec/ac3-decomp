/* Emulador de la GTE (coprocesador geometrico COP2 de la PlayStation).
 * Etapa 5, docs/10-etapa5.md. Implementado segun la especificacion psx-spx
 * (nocash): registros, banderas, saturaciones y division UNR.
 * Estado de validacion: especificacion + pruebas de coherencia
 * (psx/test/test_gte.c); SIN VERIFICAR contra hardware real. */
#ifndef PSX_GTE_H
#define PSX_GTE_H

#include <stdint.h>

typedef struct {
    int32_t d[32];      /* registros de datos (MFC2/MTC2) */
    int32_t c[32];      /* registros de control (CFC2/CTC2) */
} PsxGte;

void psx_gte_reset(PsxGte *g);
uint32_t psx_gte_read_data(PsxGte *g, int reg);     /* MFC2 / SWC2 */
void psx_gte_write_data(PsxGte *g, int reg, uint32_t v);   /* MTC2 / LWC2 */
uint32_t psx_gte_read_ctrl(PsxGte *g, int reg);     /* CFC2 */
void psx_gte_write_ctrl(PsxGte *g, int reg, uint32_t v);   /* CTC2 */
/* Ejecuta una orden COP2 (los 25 bits bajos de la instruccion). */
void psx_gte_command(PsxGte *g, uint32_t cmd);
/* Division UNR de RTPS/RTPT, expuesta para las pruebas: (h << 16) / sz3 aproximada. */
uint32_t psx_gte_unr_divide(uint32_t h, uint32_t sz3);

/* gancho opcional tras cada vertice de RTPS/RTPT (modelo -> pantalla); lo instala la
 * interpolacion de imagenes (psx_interp.h) */
/* grp identifica el objeto: resumen de la matriz de rotacion y la traslacion con que se proyecto
 * (paso 19: la interpolacion empareja por objeto) */
extern void (*psx_gte_vertex_hook)(int32_t x, int32_t y, int32_t z, int32_t sx, int32_t sy, uint32_t grp);
/* pantalla ancha (etapa 6, paso 16): factor de la x proyectada por RTPS/RTPT alrededor del centro
 * (OFX). 0.75 = una escena 16:9 dibujada en 4:3, que luego se ensancha al presentarla; 1 = normal */
extern float psx_gte_wide;
/* pruebas (etapa 6, paso 18): llamado antes (after = 0) y despues (after = 1) de cada orden COP2;
 * psx/test/gteref.c lo usa para comparar con una GTE de referencia */
extern void (*psx_gte_cmd_hook)(PsxGte *g, uint32_t cmd, int after);

/* Codigos de orden (bits 0-5) */
enum {
    GTE_RTPS = 0x01, GTE_NCLIP = 0x06, GTE_OP = 0x0C, GTE_DPCS = 0x10, GTE_INTPL = 0x11,
    GTE_MVMVA = 0x12, GTE_NCDS = 0x13, GTE_CDP = 0x14, GTE_NCDT = 0x16, GTE_NCCS = 0x1B,
    GTE_CC = 0x1C, GTE_NCS = 0x1E, GTE_NCT = 0x20, GTE_SQR = 0x28, GTE_DCPL = 0x29,
    GTE_DPCT = 0x2A, GTE_AVSZ3 = 0x2D, GTE_AVSZ4 = 0x2E, GTE_RTPT = 0x30, GTE_GPF = 0x3D,
    GTE_GPL = 0x3E, GTE_NCCT = 0x3F
};
/* Bits de orden */
#define GTE_SF (1u << 19)       /* desplazar 12 bits el resultado */
#define GTE_LM (1u << 10)       /* saturar IR a 0..0x7FFF en vez de -0x8000..0x7FFF */
/* MVMVA: matriz (bits 17-18), vector (15-16), vector de traslacion (13-14) */
#define GTE_MVMVA_ARGS(mx, v, cv) (((uint32_t)(mx) << 17) | ((uint32_t)(v) << 15) | ((uint32_t)(cv) << 13))

#endif
