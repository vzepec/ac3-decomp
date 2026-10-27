/* Capa de compatibilidad PsyQ (etapa 5): las funciones de libc cuyo resultado
 * debe ser identico al de la PS1 (p. ej. rand, que fija las secuencias del juego). */
#ifndef PSX_LIBC_H
#define PSX_LIBC_H

int psx_rand(void);
void psx_srand(unsigned int seed);
unsigned int psx_rand_seed(void);

#endif
