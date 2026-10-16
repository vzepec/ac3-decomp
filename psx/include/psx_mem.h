/* Modelo de memoria de la capa PsyQ (etapa 5, docs/10-etapa5.md): la RAM de la PS1
 * (2 MB) y el scratchpad (1 KB) son bloques del proceso, y los punteros que el juego
 * guarda en sus estructuras (listas de dibujo, etc.) son DIRECCIONES PS1 de 32 bits.
 * Asi el codigo recompilado (etapa 6) trabaja sobre la misma imagen de memoria, y la
 * comparacion con el codigo original en tools/mipsemu.py es directa. */
#ifndef PSX_MEM_H
#define PSX_MEM_H

#include <stddef.h>
#include <stdint.h>

#define PSX_RAM_SIZE 0x200000
#define PSX_SCRATCH_ADDR 0x1F800000u

extern uint8_t psx_ram[PSX_RAM_SIZE];
extern uint8_t psx_scratch[0x400];

/* direccion PS1 (KSEG0, 0x80xxxxxx) de un puntero dentro de la RAM o el scratchpad */
static inline uint32_t psx_addr(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    if (b >= psx_scratch && b < psx_scratch + sizeof psx_scratch) return PSX_SCRATCH_ADDR + (uint32_t)(b - psx_scratch);
    return 0x80000000u | (uint32_t)(b - psx_ram);
}

/* puntero a la direccion PS1 `a` (admite los espejos KUSEG/KSEG0/KSEG1 y el scratchpad) */
static inline void *psx_ptr(uint32_t a) {
    if ((a & 0xFFFFFC00u) == PSX_SCRATCH_ADDR) return psx_scratch + (a & 0x3FF);
    return psx_ram + (a & (PSX_RAM_SIZE - 1));
}

#endif
