/* Pruebas de libetc (psx/src/libetc.c): VSync y callback de VSync.
 *   psx/build/test_etc */
#include <stdio.h>
#include "psx_libapi.h"
#include "psx_libetc.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FALLO %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int calls;
static uint32_t last_addr;
static void dispatch(uint32_t addr, uint32_t a0, uint32_t a1) { calls++; last_addr = addr; }

int main(void) {
    psx_set_call(dispatch);
    psx_set_refresh(50);
    int c0 = VSync(-1);
    VSync(0);
    CHECK(VSync(-1) == c0 + 1, "VSync(0) avanza un fotograma: %d -> %d", c0, VSync(-1));
    int lines = VSync(1);
    CHECK(VSync(-1) == c0 + 1 && lines == 0, "VSync(1) no espera (lineas %d)", lines);
    VSync(0);
    lines = VSync(4);   /* hasta 4 fotogramas desde la llamada anterior: espera 4 */
    CHECK(VSync(-1) == c0 + 6 && lines == 4 * 313, "VSync(4): contador %d, lineas %d", VSync(-1) - c0, lines);
    psx_vblank();
    psx_vblank();
    lines = VSync(3);   /* ya pasaron 2: espera 1 */
    CHECK(VSync(-1) == c0 + 9 && lines == 3 * 313, "VSync(3) tras 2 fotogramas: contador %d", VSync(-1) - c0);
    VSyncCallback(0x80012345u);
    VSync(0);
    VSync(0);
    CHECK(calls == 2 && last_addr == 0x80012345u, "callback de VSync: %d llamadas a %08x", calls, last_addr);
    CHECK(VSyncCallback(0) == (int)0x80012345u, "VSyncCallback devuelve el anterior");
    VSync(0);
    CHECK(calls == 2, "sin callback no se llama");
    psx_set_refresh(60);
    VSync(0);
    CHECK(VSync(1) == 0 && VSync(0) == 263, "NTSC: 263 lineas por fotograma");
    /* contadores raiz: el 1 cuenta lineas (313 por fotograma en PAL) desde ResetRCnt */
    psx_set_refresh(50);
    SetRCnt(RCntCNT1, 0x1E00, 0x2000);
    StartRCnt(RCntCNT1);
    ResetRCnt(RCntCNT1);
    CHECK(GetRCnt(RCntCNT1) == 0, "GetRCnt tras ResetRCnt: %d", GetRCnt(RCntCNT1));
    VSync(0); VSync(0);
    CHECK(GetRCnt(RCntCNT1) == 626, "GetRCnt tras 2 fotogramas: %d", GetRCnt(RCntCNT1));
    ResetRCnt(RCntCNT1);
    CHECK(GetRCnt(RCntCNT1) == 0, "ResetRCnt");
    CHECK(GetRCnt(RCntCNT2) == 0 && SetRCnt(0x1234, 0, 0) == 0, "contadores sin modelo y especificador invalido");
    CHECK(psx_divdi3(INT64_MIN, -1) == INT64_MIN && psx_udivdi3(10, 3) == 3, "division de 64 bits en los bordes");
    printf("%d comprobaciones, %d fallos\n", checks, fails);
    return fails ? 1 : 0;
}
