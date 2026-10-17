/* Versiones del juego que lleva el APK (europea, americana o las dos: APK universal).
 * Cada una es una tabla con las direcciones de su ejecutable, su codigo traducido, su ACE.BIN y
 * el ejecutable de arranque de su disco (SYSTEM.CNF), con el que se reconoce el disco elegido.
 * Las tablas las define regions.c, compilado una vez por version (build.sh). */
#ifndef AC3_REGIONS_H
#define AC3_REGIONS_H

#include <stdint.h>

#include "recomp_rt.h"

typedef struct {
    const char *name;        /* "EU" / "US" */
    const char *boot_exe;    /* p. ej. "\\SCES_020.66;1" */
    uint32_t main, gp, bss_start, bss_end, ovl_base, lib_lo, lib_hi, lib_main;
    int refresh;             /* 50 (PAL) o 60 (NTSC) */
    const RcFunc *funcs;
    const unsigned *nfuncs;
    const RcFunc *ovl[5];
    const unsigned *novl[5];
    const RcFunc *hle;
    const unsigned *nhle;
    const uint8_t (*sig)[64];
    const uint8_t *image;    /* ACE.BIN */
    const unsigned *image_len;
} Ac3Region;

/* debiles: en un APK de una sola version, la otra no existe (direccion nula) */
extern const Ac3Region ac3_region_eu __attribute__((weak));
extern const Ac3Region ac3_region_us __attribute__((weak));

#endif
