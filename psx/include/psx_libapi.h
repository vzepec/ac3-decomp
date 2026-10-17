/* Capa de compatibilidad PsyQ (etapa 6, paso 1): lo que el juego usa de libapi (BIOS),
 * libc y libsn y no estaba en la etapa 5.
 *
 * Contadores raiz (SetRCnt/GetRCnt/StartRCnt/StopRCnt/ResetRCnt): el juego solo usa el
 * contador 1 (lineas horizontales) para medir el fotograma. Modelo por fotogramas
 * (psx_libetc.h): el valor avanza 313 (PAL) o 263 (NTSC) por VBlank desde el ultimo
 * ResetRCnt; dentro de un fotograma no avanza (no hay tiempo de CPU simulado).
 * InitHeap: el juego no llama a malloc; solo se guarda la zona.
 * printf/puts: en el codigo recompilado los argumentos son palabras PS1 (los %s son
 * direcciones); psx_printf recibe el formato y las palabras ya leidas de a1..a3 y la
 * pila. Formato identico a la printf de PsyQ (tools/psyqcheck_libc.py). */
#ifndef PSX_LIBAPI_H
#define PSX_LIBAPI_H

#include <stdint.h>

#define RCntCNT0 0xF2000000u
#define RCntCNT1 0xF2000001u
#define RCntCNT2 0xF2000002u
#define RCntCNT3 0xF2000003u

int32_t SetRCnt(uint32_t spec, uint16_t target, int32_t mode);
int32_t GetRCnt(uint32_t spec);
int32_t StartRCnt(uint32_t spec);
int32_t StopRCnt(uint32_t spec);
int32_t ResetRCnt(uint32_t spec);
void InitHeap(uint32_t *head, uint32_t size);
void EnterCriticalSection(void);
void ExitCriticalSection(void);

/* formatea como la printf de PsyQ en `out` (hasta `max` bytes, terminado en 0); devuelve
 * la longitud. args: argumentos de 32 bits en orden; los %s son direcciones PS1. */
int psx_format(char *out, int max, const char *fmt, const uint32_t *args);
/* sprintf de PsyQ: otro formateador, distinto del de printf (tools/psyqcheck_libc.py) */
int psx_sformat(char *out, int max, const char *fmt, const uint32_t *args);
/* printf/puts del juego: salida al registro del anfitrion (psx_set_tty) */
int psx_printf(const char *fmt, const uint32_t *args);
int psx_puts(const char *s);
void psx_set_tty(void (*fn)(const char *text));

/* eventos de la BIOS (handles 0xF10000nn); ver libapi.c. La tarjeta los usa para el fin de
 * las operaciones asincronas (psx_libcard.h). */
uint32_t OpenEvent(uint32_t cls, uint32_t spec, int32_t mode, uint32_t func);
int32_t CloseEvent(uint32_t h);
int32_t EnableEvent(uint32_t h);
int32_t DisableEvent(uint32_t h);
int32_t TestEvent(uint32_t h);
int32_t WaitEvent(uint32_t h);
void DeliverEvent(uint32_t cls, uint32_t spec);

/* libsn: division de 64 bits del compilador (__divdi3, __udivdi3) */
int64_t psx_divdi3(int64_t a, int64_t b);
uint64_t psx_udivdi3(uint64_t a, uint64_t b);

#endif
