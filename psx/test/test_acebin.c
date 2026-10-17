/* Prueba de psx/src/acebin.c (etapa 7): ACE.BIN leido y descomprimido de la imagen del disco
 * frente al de tools/acebin.py (data/exe/ace_eu.bin, ace_us.bin), byte a byte.
 * Uso: test_acebin <imagen.bin> <referencia.bin> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_acebin.h"
#include "psx_libcd.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "uso: %s <imagen> <referencia>\n", argv[0]); return 2; }
    if (psx_cd_open(argv[1])) { puts("no se abre la imagen"); return 1; }
    static uint8_t out[0x200000], ref[0x200000];
    PsxAceBin info;
    int r = psx_acebin_from_disc(out, sizeof out, &info);
    if (r) { printf("FALLO: codigo %d\n", r); return 1; }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { puts("no se abre la referencia"); return 1; }
    size_t n = fread(ref, 1, sizeof ref, f);
    fclose(f);
    int same = n == info.size && !memcmp(out, ref, n);
    printf("ACE.BIN: %u bytes, carga %08x, entrada %08x: %s a la referencia (%zu bytes)\n", info.size, info.load, info.entry,
           same ? "IDENTICO" : "DISTINTO", n);
    return same ? 0 : 1;
}
