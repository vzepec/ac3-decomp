/* Tabla de una version del juego (ver ac3_regions.h). build.sh compila este archivo una vez por
 * version: la europea con las direcciones de aqui; la americana con -DREGION_US y
 * -include data/recomp_us/ac3_region.h (generado por tools/recomp/region.py). En el APK
 * universal los simbolos del codigo traducido americano llevan el sufijo _us (llvm-objcopy
 * --redefine-syms en build.sh), para no chocar con los europeos. */
#include "ac3_regions.h"

#ifdef REGION_US
#define SYM2(x) x##_us
#define SYM(x) SYM2(x)
#define TABLE ac3_region_us
#define NAME "US"
#else
#define SYM(x) x
#define TABLE ac3_region_eu
#define NAME "EU"
/* el ACE.BIN europeo (las mismas que psx/recomp/native_main.c) */
#define AC3_MAIN 0x80025D08u
#define AC3_GP 0x800BA9F4u
#define AC3_BSS_START 0x800BACB0u
#define AC3_BSS_END 0x800C9DC0u
#define AC3_OVL_BASE 0x800C9DC0u
#define AC3_LIB_LO 0x80099DE0u
#define AC3_LIB_HI 0x800AE09Cu
#define AC3_LIB_MAIN 0x80025214u
#define AC3_REFRESH 50
#define AC3_RC_FUNCS rc_funcs_ace_eu
#define AC3_RC_NFUNCS rc_nfuncs_ace_eu
#define AC3_BOOT_EXE "\\SCES_020.66;1"
#endif

extern const RcFunc SYM(AC3_RC_FUNCS)[], SYM(rc_funcs_overlay1)[], SYM(rc_funcs_overlay2)[], SYM(rc_funcs_overlay3)[],
    SYM(rc_funcs_overlay4)[], SYM(rc_funcs_overlay5)[], SYM(hle_table)[];
extern const unsigned SYM(AC3_RC_NFUNCS), SYM(rc_nfuncs_overlay1), SYM(rc_nfuncs_overlay2), SYM(rc_nfuncs_overlay3),
    SYM(rc_nfuncs_overlay4), SYM(rc_nfuncs_overlay5), SYM(hle_count);
extern const uint8_t SYM(ovl_sig)[5][64];
extern const uint8_t SYM(ace_image)[];
extern const unsigned SYM(ace_image_len);

const Ac3Region TABLE = {
    .name = NAME,
    .boot_exe = AC3_BOOT_EXE,
    .main = AC3_MAIN, .gp = AC3_GP, .bss_start = AC3_BSS_START, .bss_end = AC3_BSS_END, .ovl_base = AC3_OVL_BASE,
    .lib_lo = AC3_LIB_LO, .lib_hi = AC3_LIB_HI, .lib_main = AC3_LIB_MAIN,
    .refresh = AC3_REFRESH,
    .funcs = SYM(AC3_RC_FUNCS), .nfuncs = &SYM(AC3_RC_NFUNCS),
    .ovl = {SYM(rc_funcs_overlay1), SYM(rc_funcs_overlay2), SYM(rc_funcs_overlay3), SYM(rc_funcs_overlay4), SYM(rc_funcs_overlay5)},
    .novl = {&SYM(rc_nfuncs_overlay1), &SYM(rc_nfuncs_overlay2), &SYM(rc_nfuncs_overlay3), &SYM(rc_nfuncs_overlay4),
             &SYM(rc_nfuncs_overlay5)},
    .hle = SYM(hle_table), .nhle = &SYM(hle_count),
    .sig = SYM(ovl_sig),
    .image = SYM(ace_image), .image_len = &SYM(ace_image_len),
};
