/* Versiones del juego para el modo libre (etapa 7): el codigo lo ejecuta el interprete
 * (mipsi.c) desde la RAM y ACE.BIN se lee del disco (psx_acebin.h), asi que de cada version solo
 * hacen falta direcciones (dato de analisis: tools/recomp/region.py y docs/03-versiones.md) y su
 * enlace con la libreria (hle_eu.c, hle_us.c). Nada de esto es codigo ni bytes de Namco. */
#ifndef AC3_VERSIONS_H
#define AC3_VERSIONS_H

#include <stdint.h>

#include "recomp_rt.h"

typedef struct {
    const char *name;       /* "EU" / "US" */
    const char *boot_exe;   /* ejecutable de arranque del disco (SYSTEM.CNF) */
    uint32_t main, gp, bss_start, bss_end, ovl_base, lib_lo, lib_hi, lib_main, pad_buf;
    uint32_t g_state_ptr;   /* puntero al estado de la partida (mision actual en +0x390, short) */
    uint32_t mission_start; /* funcion que arranca la mision: registro = mission_records[+0x390] */
    uint32_t seq_start;     /* primer paso del modo 5: secuencia segun g_state + 0x3AC (0 demo, 1, 2 final) */
    uint32_t frustum;       /* cono de vision del terreno (a0 + 4: ancho de pantalla, short) */
    uint32_t frustum_next;  /* la funcion que le sigue (ya no lee el ancho) */
    int refresh;            /* 50 (PAL) / 60 (NTSC) */
    const RcFunc *hle;
    const unsigned *nhle;
} Ac3Version;

extern const Ac3Version ac3_versions[];
extern const int ac3_nversions;
/* la version del disco abierto (psx_cd_open), por su ejecutable de arranque; NULL si ninguna */
const Ac3Version *ac3_version_detect(void);
/* pantalla ancha (etapa 6, paso 16): con psx_gte_wide < 1, el cono de vision del terreno se
 * calcula con el ancho de pantalla / psx_gte_wide (si no, faltan casillas en los lados). Va por
 * enganches del interprete (mipsi_set_hook) */
void ac3_wide_install(const Ac3Version *v);
/* veces que el juego ha calculado el cono de vision del terreno (una por imagen con terreno: la
 * mision). Sirve para activar la pantalla ancha solo en la mision */
extern volatile uint32_t ac3_terrain_count;

#endif
