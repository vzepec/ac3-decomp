/* Tabla de versiones del modo libre (ver ac3_versions.h). Direcciones de la EU: las de la etapa
 * 6 (docs/11-etapa6.md); de la US: tools/recomp/region.py (data/recomp_us/region.json),
 * validadas con 0 funciones distintas en el comparador. Estado de la partida y arranque de
 * mision: decompilacion de Ghidra (EU FUN_800355f0 / US FUN_800357b8 leen mission_records
 * con el short de g_state + 0x390; docs/11-etapa6.md, paso 11). */
#include "ac3_versions.h"
#include "psx_libcd.h"

extern const RcFunc hle_table_eu[], hle_table_us[];
extern const unsigned hle_count_eu, hle_count_us;

const Ac3Version ac3_versions[] = {
    {"EU", "\\SCES_020.66;1", 0x80025D08u, 0x800BA9F4u, 0x800BACB0u, 0x800C9DC0u, 0x800C9DC0u, 0x80099DE0u, 0x800AE09Cu,
     0x80025214u, 0x800BAA04u, 0x800BACD0u, 0x800355F0u, 0x800563C8u, 50, hle_table_eu, &hle_count_eu},
    {"US", "\\SLUS_009.72;1", 0x80025E5Cu, 0x800BB454u, 0x800BB808u, 0x800CA908u, 0x800CA908u, 0x8009A1B4u, 0x800AE470u,
     0x80025394u, 0x800BB464u, 0x800BB828u, 0x800357B8u, 0x8005658Cu, 60, hle_table_us, &hle_count_us},
};
const int ac3_nversions = 2;

const Ac3Version *ac3_version_detect(void) {
    for (int i = 0; i < ac3_nversions; i++) {
        CdlFILE f;
        if (CdSearchFile(&f, ac3_versions[i].boot_exe)) return &ac3_versions[i];
    }
    return 0;
}
