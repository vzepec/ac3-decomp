/* Evalua las funciones de psx/ para tools/psyqcheck.py.
 *   psx/build/test_math <ace.bin> < entradas
 * Cada linea: "<funcion> <arg1> [arg2]"; imprime el resultado (uno por linea). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx_libc.h"
#include "psx_libgte.h"

int main(int argc, char **argv) {
    uint8_t *buf = NULL;
    size_t len = 0;
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            len = (size_t)ftell(f);
            fseek(f, 0, SEEK_SET);
            buf = malloc(len);
            if (fread(buf, 1, len, f) != len) len = 0;
            fclose(f);
        }
    }
    int r = psx_gte_tables_init(buf, len);
    fprintf(stderr, "tabla de arcotangente: %s\n", r == 0 ? "leida de ACE.BIN" : "NO encontrada (aproximada)");
    char fn[32];
    long a, b;
    char line[128];
    while (fgets(line, sizeof line, stdin)) {
        a = b = 0;
        if (sscanf(line, "%31s %ld %ld", fn, &a, &b) < 1) continue;
        long v = 0;
        if (!strcmp(fn, "rsin")) v = rsin((int)a);
        else if (!strcmp(fn, "rcos")) v = rcos((int)a);
        else if (!strcmp(fn, "SquareRoot0")) v = SquareRoot0(a);
        else if (!strcmp(fn, "ratan2")) v = ratan2(a, b);
        else if (!strcmp(fn, "srand")) { psx_srand((unsigned)a); v = 0; }
        else if (!strcmp(fn, "rand")) v = psx_rand();
        printf("%ld\n", v);
    }
    free(buf);
    return 0;
}
