/* Lectura de los datos del disco de Ace Combat 3 en el dispositivo.
 *
 * Port a C de tools/psxiso.py, tools/acepack.py, tools/ulz.py y tools/acerec.py.
 * La app NO lleva datos del juego: todo se lee de la imagen de disco del
 * usuario en tiempo de ejecucion (docs/06-legal.md).
 */
#ifndef ACE_H
#define ACE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *f;
    int sector_size;   /* 2352 o 2048 */
    int data_offset;   /* 24 (MODE2 form 1), 16 (MODE1) o 0 */
} Disc;

typedef struct {
    uint32_t lba, size;
} DiscFile;

int disc_open(Disc *d, const char *path);
void disc_close(Disc *d);
int disc_read(Disc *d, uint32_t lba, void *buf, uint32_t nbytes);
int disc_find(Disc *d, const char *name, DiscFile *out);
int disc_volume_is_ac3(Disc *d);

typedef struct {
    uint32_t count;
    uint32_t *tag, *sector;
    DiscFile pack;
} AcePack;

/* Abre ACE.BPH/ACE.BPB. */
int pack_open(Disc *d, AcePack *p);
void pack_free(AcePack *p);
/* Lee el registro i del BPB (tamano = (tag & 0xFFFFFF) * 4). malloc. */
uint8_t *pack_read_record(Disc *d, AcePack *p, uint32_t i, uint32_t *size);

/* ULZ (variantes 0 y 2). Devuelve buffer malloc y su tamano, o NULL. */
uint8_t *ulz_decompress(const uint8_t *buf, size_t len, uint32_t *out_size);
int is_ulz(const uint8_t *p);

/* Tabla de offsets generica: u32 count; u32 off[count]; off[0] == 4+4*count. */
uint32_t offset_table(const uint8_t *buf, uint32_t len, uint32_t *starts, uint32_t *ends, uint32_t max);

/* Registro: u32 tipo; u32 off[n+1]. Seccion -1 = preludio [off[0], off[1]). */
typedef struct {
    const uint8_t *raw;
    uint32_t size, type, nsec;      /* nsec sin contar el preludio */
    uint32_t off[64];               /* off[0] = preludio, off[k+1] = seccion k */
} Record;

int record_parse(Record *r, const uint8_t *raw, uint32_t size);
/* Seccion k (o -1 = preludio): puntero y tamano en crudo. */
const uint8_t *record_section(const Record *r, int k, uint32_t *len);

#endif
