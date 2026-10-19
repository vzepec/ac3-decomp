/* Tarjeta de memoria (etapa 6, paso 1): libcard y las funciones de archivo de la BIOS
 * sobre "buXY:" (ver psx_libcard.h). Formato de la imagen: el estandar de 128 KB (.mcd,
 * el mismo que usan los emuladores): bloque 0 = cabecera "MC" (marco 0) y directorio
 * (marcos 1-15, uno por bloque de datos), bloques 1-15 de 8 KB con los archivos. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_libapi.h"
#include "psx_libcard.h"
#include "psx_mem.h"

#define CARD_SIZE (128 * 1024)
#define BLOCK 8192
#define FRAME 128
#define MAX_FD 8

static struct { int present; char path[512]; uint8_t img[CARD_SIZE]; } C[2];
static struct { int used, port, first, async; uint32_t pos, size; } F[MAX_FD];
static struct { int port, next; char pat[24]; } DIRS;   /* estado de firstfile/nextfile */
static int g_started;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void frame_sum(uint8_t *f) {
    uint8_t x = 0;
    for (int i = 0; i < 127; i++) x ^= f[i];
    f[127] = x;
}

static void card_format(int port) {
    uint8_t *m = C[port].img;
    memset(m, 0, CARD_SIZE);
    m[0] = 'M'; m[1] = 'C';
    frame_sum(m);
    for (int b = 1; b < 16; b++) {
        uint8_t *d = m + b * FRAME;
        wr32(d, 0xA0);
        d[8] = d[9] = 0xFF;
        frame_sum(d);
    }
    for (int k = 16; k < 36; k++) {   /* lista de sectores defectuosos: vacia */
        uint8_t *d = m + k * FRAME;
        wr32(d, 0xFFFFFFFFu);
        d[8] = d[9] = 0xFF;
        frame_sum(d);
    }
    memcpy(m + 63 * FRAME, m, FRAME);   /* marco de prueba de escritura */
}

static void card_flush(int port) {
    if (!C[port].present || !C[port].path[0]) return;
    FILE *f = fopen(C[port].path, "wb");
    if (!f) return;
    fwrite(C[port].img, 1, CARD_SIZE, f);
    fclose(f);
}

int psx_card_attach(int port, const char *path) {
    if (port < 0 || port > 1) return -1;
    C[port].present = 1;
    snprintf(C[port].path, sizeof C[port].path, "%s", path ? path : "");
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (f && fread(C[port].img, 1, CARD_SIZE, f) == CARD_SIZE && C[port].img[0] == 'M' && C[port].img[1] == 'C') {
        fclose(f);
        return 0;
    }
    if (f) fclose(f);
    card_format(port);   /* sin archivo valido: tarjeta nueva formateada */
    card_flush(port);
    return 1;
}
void psx_card_detach(int port) { if (port == 0 || port == 1) C[port].present = 0; }
const uint8_t *psx_card_image(int port) { return port == 0 || port == 1 ? C[port].img : NULL; }

/* "buXY:NOMBRE" -> puerto (X) y nombre */
static int parse(const char *name, const char **file) {
    if (strncmp(name, "bu", 2) || !name[2] || !name[3] || name[4] != ':') return -1;
    int port = name[2] - '0';
    if (port < 0 || port > 1 || !C[port].present) return -1;
    *file = name + 5;
    return port;
}

static uint8_t *dir(int port, int b) { return C[port].img + b * FRAME; }   /* b = 1..15 */

static int find(int port, const char *file) {
    for (int b = 1; b < 16; b++) {
        uint8_t *d = dir(port, b);
        if (rd32(d) == 0x51 && !strncmp((const char *)d + 10, file, 20)) return b;
    }
    return -1;
}

static uint8_t *data_at(int port, int first, uint32_t pos) {   /* sigue la cadena de bloques */
    int b = first;
    for (uint32_t k = pos / BLOCK; k && b > 0; k--) {
        uint16_t nx = (uint16_t)(dir(port, b)[8] | dir(port, b)[9] << 8);
        b = nx == 0xFFFF ? -1 : nx + 1;
    }
    return b > 0 ? C[port].img + b * BLOCK + pos % BLOCK : NULL;
}

void InitCARD(int32_t pad_enable) { (void)pad_enable; }
int32_t StartCARD(void) { g_started = 1; return 1; }
int32_t StopCARD(void) { g_started = 0; return 1; }
void _bu_init(void) {}

/* _card_info/_card_load/_card_clear (los llaman los overlays). Segun la referencia de
 * PsyQ, _card_info y _card_load avisan con eventos de software (SwCARD, 0xF4000001) y
 * _card_clear, que escribe directamente en la tarjeta, con eventos de hardware (HwCARD,
 * 0xF0000011). Con tarjeta: fin de la operacion (EvSpIOE); sin ella: tiempo agotado
 * (EvSpTIMOUT). Devuelven 1 (operacion aceptada). */
/* depuracion: PSX_CARDDBG=1 imprime las llamadas del juego a la tarjeta */
#define CDBG(...) do { if (getenv("PSX_CARDDBG")) { fprintf(stderr, "tarjeta: " __VA_ARGS__); } } while (0)
static int32_t card_op(int32_t chan, uint32_t cls) {
    int port = (chan >> 4) & 1;
    CDBG("card_op canal %x clase %08x\n", (unsigned)chan, cls);
    DeliverEvent(cls, C[port].present ? 0x0004 : 0x0100);
    return 1;
}
int32_t _card_info(int32_t chan) { return card_op(chan, 0xF4000001u); }
int32_t _card_load(int32_t chan) { return card_op(chan, 0xF4000001u); }
int32_t _card_clear(int32_t chan) { return card_op(chan, 0xF0000011u); }

int32_t _card_format(int32_t chan) {
    int port = (chan >> 4) & 1;
    if (!C[port].present) return 0;
    card_format(port);
    card_flush(port);
    return 1;
}

/* open: FREAD 1, FWRITE 2, FCREAT 0x200 (bloques en los bits 16+), FASYNC 0x8000 */
static int32_t bios_open_(const char *name, int32_t mode);
int32_t bios_open(const char *name, int32_t mode) {
    int32_t r = bios_open_(name, mode);
    CDBG("open(\"%s\", %#x) = %d\n", name, (unsigned)mode, r);
    return r;
}
static int32_t bios_open_(const char *name, int32_t mode) {
    const char *file;
    int port = parse(name, &file);
    if (port < 0) return -1;
    int first = find(port, file);
    if (mode & 0x200) {   /* crear: falla si existe o no caben los bloques */
        int n = (mode >> 16) & 0xFFFF;
        if (first > 0 || n < 1 || n > 15) return -1;
        int blocks[15], k = 0;
        for (int b = 1; b < 16 && k < n; b++) if ((rd32(dir(port, b)) & 0xF0) == 0xA0) blocks[k++] = b;
        if (k < n) return -1;
        for (int i = 0; i < n; i++) {
            uint8_t *d = dir(port, blocks[i]);
            memset(d, 0, FRAME);
            wr32(d, i == 0 ? 0x51 : i == n - 1 ? 0x53 : 0x52);
            if (i == 0) { wr32(d + 4, (uint32_t)n * BLOCK); snprintf((char *)d + 10, 21, "%s", file); }
            uint16_t nx = i + 1 < n ? (uint16_t)(blocks[i + 1] - 1) : 0xFFFF;
            d[8] = (uint8_t)nx; d[9] = (uint8_t)(nx >> 8);
            frame_sum(d);
            memset(C[port].img + blocks[i] * BLOCK, 0, BLOCK);
        }
        if (n == 1) { wr32(dir(port, blocks[0]), 0x51); frame_sum(dir(port, blocks[0])); }
        first = blocks[0];
        card_flush(port);
    }
    if (first < 0) return -1;
    for (int fd = 0; fd < MAX_FD; fd++)
        if (!F[fd].used) {
            F[fd].used = 1; F[fd].port = port; F[fd].first = first; F[fd].pos = 0;
            F[fd].size = rd32(dir(port, first) + 4);
            F[fd].async = (mode & 0x8000) != 0;
            return fd + 2;   /* la BIOS no da 0 ni 1 a los archivos */
        }
    return -1;
}

static int32_t rw(int32_t fd, uint8_t *buf, int32_t n, int wr) {
    fd -= 2;
    if (fd < 0 || fd >= MAX_FD || !F[fd].used || n < 0) return -1;
    int32_t done = 0;
    while (done < n && F[fd].pos < F[fd].size) {
        uint8_t *p = data_at(F[fd].port, F[fd].first, F[fd].pos);
        if (!p) break;
        int32_t chunk = BLOCK - (int32_t)(F[fd].pos % BLOCK);
        if (chunk > n - done) chunk = n - done;
        if (wr) memcpy(p, buf + done, (size_t)chunk); else memcpy(buf + done, p, (size_t)chunk);
        done += chunk;
        F[fd].pos += (uint32_t)chunk;
    }
    if (F[fd].async) DeliverEvent(0xF4000001u, 0x0004);   /* SwCARD, EvSpIOE: fin de la operacion */
    return done;
}

int32_t bios_read(int32_t fd, void *buf, int32_t n) { int32_t r = rw(fd, buf, n, 0); CDBG("read(%d, %d) = %d\n", fd, n, r); return r; }
int32_t bios_write(int32_t fd, const void *buf, int32_t n) {
    int32_t r = rw(fd, (uint8_t *)(uintptr_t)buf, n, 1);
    CDBG("write(%d, %d) = %d\n", fd, n, r);
    if (r >= 0) card_flush(F[fd - 2].port);
    return r;
}
int32_t bios_close(int32_t fd) {
    fd -= 2;
    if (fd < 0 || fd >= MAX_FD || !F[fd].used) return -1;
    F[fd].used = 0;
    return fd + 2;
}

static int match(const char *pat, const char *name) {   /* '*' y '?' como la BIOS */
    if (!strcmp(pat, "*.*") || !strcmp(pat, "*")) return 1;
    for (; *pat; pat++, name++) {
        if (*pat == '*') return 1;
        if (*pat != '?' && *pat != *name) return 0;
        if (!*name) return 0;
    }
    return !*name;
}

static DIRENTRY *dir_next(DIRENTRY *d) {
    for (int b = DIRS.next; b < 16; b++) {
        uint8_t *e = dir(DIRS.port, b);
        if (rd32(e) != 0x51 || !match(DIRS.pat, (const char *)e + 10)) continue;
        memset(d, 0, sizeof *d);
        memcpy(d->name, e + 10, 20);
        d->attr = (int32_t)rd32(e);
        d->size = (int32_t)rd32(e + 4);
        d->head = b;
        DIRS.next = b + 1;
        return d;
    }
    return NULL;
}

DIRENTRY *bios_firstfile(const char *name, DIRENTRY *d) {
    const char *pat;
    int port = parse(name, &pat);
    CDBG("firstfile(\"%s\") puerto %d\n", name, port);
    if (port < 0) return NULL;
    DIRS.port = port;
    DIRS.next = 1;
    snprintf(DIRS.pat, sizeof DIRS.pat, "%s", pat);
    return dir_next(d);
}
DIRENTRY *bios_nextfile(DIRENTRY *d) { return dir_next(d); }

int32_t bios_erase(const char *name) {
    const char *file;
    int port = parse(name, &file);
    int b = port < 0 ? -1 : find(port, file);
    if (b < 0) return 0;
    while (b > 0) {
        uint8_t *d = dir(port, b);
        uint16_t nx = (uint16_t)(d[8] | d[9] << 8);
        wr32(d, (rd32(d) & 0x0F) | 0xA0);
        frame_sum(d);
        b = nx == 0xFFFF ? -1 : nx + 1;
    }
    card_flush(port);
    return 1;
}
