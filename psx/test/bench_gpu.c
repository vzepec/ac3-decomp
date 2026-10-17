/* Banco de pruebas de la GPU por software (etapa 6, paso 5): redibuja N veces un fotograma real
 * guardado con PSX_GPU_DUMP (direccion de la lista + estado de la GPU + RAM de 2 MB; el volcado
 * contiene datos del juego: va a data/) y mide el tiempo; escribe un resumen (suma) de la VRAM
 * para comprobar que una optimizacion no cambia el resultado.
 *   bench_gpu <volcado> [veces] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "psx_gpu.h"
#include "psx_mem.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s <volcado> [veces]\n", argv[0]); return 2; }
    int times = argc > 2 ? atoi(argv[2]) : 100;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    uint32_t ot;
    static PsxGpu g0, g;
    if (fread(&ot, 4, 1, f) != 1 || fread(&g0, sizeof g0, 1, f) != 1 || fread(psx_ram, 1, 0x200000, f) != 0x200000) return 1;
    fclose(f);
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < times; i++) { memcpy(&g, &g0, sizeof g); psx_gpu_draw_list(&g, ot); }
    clock_gettime(CLOCK_MONOTONIC, &b);
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < PSX_VRAM_W * PSX_VRAM_H; i++) h = (h ^ g.vram[i]) * 1099511628211ull;
    double ms = ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) * 1e-6) / times;
    printf("%.3f ms por imagen; %u primitivas, %u pixeles; resumen de la VRAM %016llx\n", ms, g.prims_drawn - g0.prims_drawn,
           g.pixels_drawn - g0.pixels_drawn, (unsigned long long)h);
    return 0;
}
