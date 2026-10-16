/* libpad en modo directo (ver psx_libpad.h). */
#include <string.h>

#include "psx_libpad.h"

static struct {
    uint8_t *buf[2];
    int active, updated, enable;
    struct { int id; uint16_t buttons; uint8_t lx, ly, rx, ry; } pad[2];
} P = {.pad = {{0x41, 0, 0x80, 0x80, 0x80, 0x80}, {0, 0, 0x80, 0x80, 0x80, 0x80}}};

void psx_pad_connect(int port, int id) { if (port == 0 || port == 1) P.pad[port].id = id; }

void psx_pad_set(int port, uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry) {
    if (port != 0 && port != 1) return;
    P.pad[port].buttons = buttons;
    P.pad[port].lx = lx; P.pad[port].ly = ly; P.pad[port].rx = rx; P.pad[port].ry = ry;
}

/* llamado en cada VBlank por psx_vblank */
void psx_pad_frame(void) {
    if (!P.active) return;
    for (int i = 0; i < 2; i++) {
        uint8_t *b = P.buf[i];
        if (!b) continue;
        if (!P.pad[i].id) { b[0] = 0xFF; b[1] = 0xFF; continue; }
        uint16_t raw = (uint16_t)~P.pad[i].buttons;
        b[0] = 0x00;
        b[1] = (uint8_t)P.pad[i].id;
        b[2] = (uint8_t)raw; b[3] = (uint8_t)(raw >> 8);
        if (P.pad[i].id == 0x73) { b[4] = P.pad[i].rx; b[5] = P.pad[i].ry; b[6] = P.pad[i].lx; b[7] = P.pad[i].ly; }
    }
    P.updated = 1;
}

void PadInitDirect(uint8_t *pad1, uint8_t *pad2) {
    P.buf[0] = pad1; P.buf[1] = pad2;
    if (pad1) memset(pad1, 0xFF, 2);
    if (pad2) memset(pad2, 0xFF, 2);
}
void PadStartCom(void) { P.active = 1; }
void PadStopCom(void) { P.active = 0; P.updated = 0; }
int PadEnableCom(int mode) { int o = P.enable; P.enable = mode; return o; }
/* 1 si hubo comunicacion con el mando en el VSync actual (referencia de PsyQ: "detect
 * whether the controller communication was completed in the current V-sync"); con la
 * comunicacion activa es 1 desde el primer fotograma. No verificado contra el original. */
int PadChkVsync(void) { return P.updated; }

static int port_index(int port) { return port & 0x10 ? 1 : 0; }   /* 0x00 puerto 1, 0x10 puerto 2 */

/* 0 = PadStateDiscon, 2 = PadStateFindCTP1 (mando estandar identificado). Las tomas de
 * multitap (puerto & 0x0F != 0) no existen. */
int PadGetState(int port) {
    if (port & 0x0F) return 0;
    return P.active && P.pad[port_index(port)].id ? 2 : 0;
}

/* term 1 = InfoModeCurID (nibble alto del id: 4 digital, 7 analogico); el resto, 0 */
int PadInfoMode(int port, int term, int offs) {
    if (PadGetState(port) == 0) return 0;
    if (term == 1) return P.pad[port_index(port)].id >> 4;
    return 0;
}
int PadInfoAct(int port, int actno, int term) { return 0; }
int PadSetMainMode(int port, int offs, int lock) { return 0; }
void PadSetAct(int port, uint8_t *data, int len) {}
int PadSetActAlign(int port, const uint8_t *data) { return 0; }
