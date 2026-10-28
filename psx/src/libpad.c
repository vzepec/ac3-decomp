/* libpad en modo directo (ver psx_libpad.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_libpad.h"

static struct {
    uint8_t *buf[2];
    uint8_t *act[2];       /* bufer de los motores que registra el juego (PadSetAct) y su largo */
    int actlen[2];
    uint8_t align[2][6];   /* PadSetActAlign: byte del bufer de cada motor (0xFF: ninguno) */
    int active, updated, enable;
    struct { int id; uint16_t buttons; uint8_t lx, ly, rx, ry; } pad[2];
} P = {.pad = {{0x41, 0, 0x80, 0x80, 0x80, 0x80}, {0, 0, 0x80, 0x80, 0x80, 0x80}}};

void psx_pad_connect(int port, int id) { if (port == 0 || port == 1) P.pad[port].id = id; }

void psx_pad_set(int port, uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry) {
    if (port != 0 && port != 1) return;
    P.pad[port].buttons = buttons;
    P.pad[port].lx = lx; P.pad[port].ly = ly; P.pad[port].rx = rx; P.pad[port].ry = ry;
}

void psx_pad_peek(int port, uint16_t *buttons, uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry) {
    if (port != 0 && port != 1) return;
    *buttons = P.pad[port].buttons;
    *lx = P.pad[port].lx; *ly = P.pad[port].ly; *rx = P.pad[port].rx; *ry = P.pad[port].ry;
}

static int dbg(const char *f);
extern int VSync(int);

int psx_pad_motor(int port) {
    if (port != 0 && port != 1 || !P.act[port] || !P.pad[port].id) return 0;
    int big = 0, small = 0;
    for (int k = 0; k < P.actlen[port] && k < 6; k++) {   /* align[k]: el motor al que va el byte k */
        if (P.align[port][k] == 1) big = P.act[port][k];
        else if (P.align[port][k] == 0) small = P.act[port][k] != 0;
    }
    return big ? big : small ? 160 : 0;
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
    static int last = -1;
    if (P.act[0] && P.actlen[0] >= 1 && P.act[0][0] != last) {
        last = P.act[0][0];
        if (dbg("motores")) printf("libpad: motores %02x (largo %d) vb %d\n", last, P.actlen[0], VSync(-1));
    }
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

/* depuracion (PSX_PAD_LOG=1): las llamadas del juego a libpad, las primeras de cada tipo */
static int dbg(const char *f) {
    static int on = -1, n[8];
    static const char *names[8];
    if (on < 0) on = getenv("PSX_PAD_LOG") != NULL;
    if (!on) return 0;
    int k = 0;
    while (k < 8 && names[k] && strcmp(names[k], f)) k++;
    if (k == 8) return 0;
    names[k] = f;
    return ++n[k] <= 40;
}

static int port_index(int port) { return port & 0x10 ? 1 : 0; }   /* 0x00 puerto 1, 0x10 puerto 2 */

/* 0 = PadStateDiscon, 2 = PadStateFindCTP1 (mando estandar identificado), 6 = PadStateStable
 * (DualShock con los motores listos: el analogico, 0x73). Las tomas de multitap (puerto & 0x0F
 * != 0) no existen. */
static int dualshock(int i) { return P.pad[i].id == 0x73; }
int PadGetState(int port) {
    if (dbg("PadGetState")) printf("libpad: PadGetState(%#x) vb %d\n", port, VSync(-1));
    if (port & 0x0F) return 0;
    int i = port_index(port);
    return P.active && P.pad[i].id ? (dualshock(i) ? 6 : 2) : 0;
}

/* term 1 = InfoModeCurID (nibble alto del id: 4 digital, 7 analogico); con el DualShock (etapa 7,
 * paso 9: vibracion), 2 = InfoModeCurExID (el id ampliado: 7), 3 = InfoModeCurExOffs (su posicion en
 * la tabla: 1) y 4 = InfoModeIdTable (offs -1: 2 modos; 0: 4 digital, 1: 7 analogico) */
int PadInfoMode(int port, int term, int offs) {
    if (dbg("PadInfoMode")) printf("libpad: PadInfoMode(%#x, %d, %d)\n", port, term, offs);
    if (PadGetState(port) == 0) return 0;
    int i = port_index(port);
    if (term == 1) return P.pad[i].id >> 4;
    if (!dualshock(i)) return 0;
    if (term == 2) return 7;
    if (term == 3) return 1;
    if (term == 4) return offs == -1 ? 2 : offs == 0 ? 4 : offs == 1 ? 7 : 0;
    return 0;
}
/* motores del DualShock: actno -1, cuantos (2); de cada uno, term 1 = InfoActFunc (1, motor),
 * 2 = InfoActSub (0 el pequeno, 1 el grande), 3 = InfoActSize (0: encendido/apagado; 1: un byte de
 * fuerza), 4 = InfoActCurr (consumo: 10 y 20) */
int PadInfoAct(int port, int actno, int term) {
    if (dbg("PadInfoAct")) printf("libpad: PadInfoAct(%#x, %d, %d)\n", port, actno, term);
    if (PadGetState(port) == 0 || !dualshock(port_index(port))) return 0;
    if (actno == -1) return 2;
    if (actno < 0 || actno > 1) return 0;
    if (term == 1) return 1;
    if (term == 2) return actno;
    if (term == 3) return actno;
    if (term == 4) return actno ? 20 : 10;
    return 0;
}
int PadSetMainMode(int port, int offs, int lock) { if (dbg("PadSetMainMode")) printf("libpad: PadSetMainMode(%#x, %d, %d)\n", port, offs, lock); return 0; }
void PadSetAct(int port, uint8_t *data, int len) {
    if (dbg("PadSetAct")) printf("libpad: PadSetAct(%#x, %p, %d) vb %d\n", port, (void *)data, len, VSync(-1));
    int i = port_index(port);
    P.act[i] = data; P.actlen[i] = len;
}
/* el reparto de los bytes del bufer entre los motores: align[k] = byte del motor k (0xFF: ninguno).
 * Devuelve 1 (hecho) con el DualShock */
int PadSetActAlign(int port, const uint8_t *data) {
    if (dbg("PadSetActAlign")) printf("libpad: PadSetActAlign(%#x, %02x %02x %02x %02x %02x %02x) vb %d\n", port, data[0], data[1], data[2], data[3], data[4], data[5], VSync(-1));
    int i = port_index(port);
    if (PadGetState(port) == 0 || !dualshock(i)) return 0;
    memcpy(P.align[i], data, 6);
    return 1;
}
