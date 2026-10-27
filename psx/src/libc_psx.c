/* libc de PsyQ: solo lo que debe dar resultados identicos a la PS1.
 * rand/srand: ACE.BIN EU 0x800AC178 / 0x800AC1A8 (semilla en 0x800C9D30). */
#include "psx_libc.h"

static unsigned int seed;

int psx_rand(void) {
    seed = seed * 0x41C64E6Du + 0x3039u;
    return (int)((seed >> 16) & 0x7FFF);
}

void psx_srand(unsigned int s) { seed = s; }
/* etapa 9, paso 1: la semilla es estado de la partida fuera de la RAM (el redibujo la copia y la huella la incluye) */
unsigned int psx_rand_seed(void) { return seed; }
