/* RAM y scratchpad de la PS1 (ver psx_mem.h). */
#include "psx_mem.h"

uint8_t psx_ram[PSX_RAM_SIZE];
uint8_t psx_scratch[0x400];

/* etapa 9, paso 1 (determinismo): huella de la RAM en 32 bloques de 64 KB (h[0..31]) y la del
 * scratchpad (h[32]). Misma partida y mismos mandos -> mismas huellas (tools/determinismo.py) */
void psx_mem_hash(uint64_t h[33]) {
    for (int b = 0; b < 33; b++) {
        const uint64_t *p = (const uint64_t *)(b < 32 ? psx_ram + b * 0x10000 : psx_scratch);
        int n = (b < 32 ? 0x10000 : 0x400) / 8;
        uint64_t x = 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < n; i++) x = (x ^ p[i]) * 0x100000001B3ull;
        h[b] = x;
    }
}
