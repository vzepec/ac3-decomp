/* Capa de compatibilidad PsyQ (etapa 6, paso 1): tarjeta de memoria.
 * libcard (InitCARD, StartCARD, StopCARD, _bu_init, _card_format) y las funciones de
 * archivo de la BIOS sobre "buXY:" que usa el juego (open, read, write, close,
 * firstfile, nextfile, erase), sobre imagenes .mcd de 128 KB (formato estandar de los
 * emuladores) que el anfitrion asocia a cada puerto.
 * Operaciones asincronas (FASYNC): terminan al momento y dejan pendiente el evento SwCARD
 * EvSpIOE (0xF4000001, 0x0004), que el juego recoge con TestEvent.
 * read/write copian exactamente los bytes pedidos (el juego usa 0x11FC); SIN VERIFICAR si
 * la BIOS los redondea a sectores de 128 bytes.
 * Sin tarjeta en el puerto: open devuelve -1 y firstfile NULL. */
#ifndef PSX_LIBCARD_H
#define PSX_LIBCARD_H

#include <stdint.h>

typedef struct {
    char name[20];
    int32_t attr, size;
    uint32_t next;      /* direccion PS1 (0) */
    int32_t head;
    char system[4];
} DIRENTRY;             /* 40 bytes */

/* ---- anfitrion ---- */
/* asocia la imagen `path` al puerto (0/1); si no existe o no es valida, crea una formateada.
 * Devuelve 0 si se cargo, 1 si se creo */
int psx_card_attach(int port, const char *path);
void psx_card_detach(int port);
/* (etapa 10, paso 8) asocia una imagen de 128 KB que no es de un archivo (la tarjeta de quien crea la partida en red,
 * recibida): lo que se guarde se queda en la memoria */
void psx_card_attach_image(int port, const uint8_t *img);
const uint8_t *psx_card_image(int port);

/* ---- libcard ---- */
void InitCARD(int32_t pad_enable);
int32_t StartCARD(void);
int32_t StopCARD(void);
void _bu_init(void);
int32_t _card_format(int32_t chan);
int32_t _card_info(int32_t chan);
int32_t _card_load(int32_t chan);
int32_t _card_clear(int32_t chan);

/* ---- BIOS (archivos en la tarjeta) ----
 * Con prefijo bios_ para no chocar con las de POSIX del anfitrion; el codigo del juego
 * las llama como open/read/write/close/firstfile/nextfile/erase. */
int32_t bios_open(const char *name, int32_t mode);
int32_t bios_read(int32_t fd, void *buf, int32_t n);
int32_t bios_write(int32_t fd, const void *buf, int32_t n);
int32_t bios_close(int32_t fd);
DIRENTRY *bios_firstfile(const char *name, DIRENTRY *d);
DIRENTRY *bios_nextfile(DIRENTRY *d);
int32_t bios_erase(const char *name);

#endif
