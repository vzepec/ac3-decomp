/* Capa de compatibilidad PsyQ (etapa 5): libcd sobre una imagen de disco (.bin de 2352
 * bytes por sector o .iso de 2048). Modelo de tiempo e interrupciones: psx_libetc.h.
 *
 * Que se reproduce: los comandos del controlador que usa el juego (Setloc, Setmode,
 * Setfilter, ReadN, ReadS, Pause, Stop, Standby, Nop, GetlocL/P, GetTN), su terminacion
 * diferida (callback de CdSyncCallback con CdlComplete), los sectores a la velocidad de
 * lectura (75 o 150 por segundo: 3 por fotograma a doble velocidad en PAL) con el
 * callback de CdReadyCallback, la lectura del sector con CdGetSector en tamano 2048 o
 * 2340 segun el modo, el filtro de XA-ADPCM en ReadS (los sectores de audio no llegan al
 * programa: van a un sumidero para libspu) y CdSearchFile sobre el ISO9660.
 * Simplificaciones: CdRead es sincrono (copia de inmediato); no hay tiempos de busqueda.
 *
 * Los buferes de resultado que reciben los callbacks y el CdlLOC de CdLastPos viven en
 * la RAM de la PS1 en las MISMAS direcciones que usa la libcd original del ACE.BIN
 * europeo (0x800C5C98, 0x800C5CA0, 0x800B40F4), porque el juego recibe sus punteros. */
#ifndef PSX_LIBCD_H
#define PSX_LIBCD_H

#include <stdint.h>

typedef struct { uint8_t minute, second, sector, track; } CdlLOC;
typedef struct { CdlLOC pos; uint32_t size; char name[16]; } CdlFILE;
typedef struct { uint8_t val0, val1, val2, val3; } CdlATV;
typedef uint32_t CdlCB;   /* direccion PS1 de la funcion (psx_libetc.h) */

enum { CdlNop = 1, CdlSetloc = 2, CdlPlay = 3, CdlForward = 4, CdlBackward = 5, CdlReadN = 6, CdlStandby = 7,
       CdlStop = 8, CdlPause = 9, CdlMute = 11, CdlDemute = 12, CdlSetfilter = 13, CdlSetmode = 14,
       CdlGetparam = 15, CdlGetlocL = 16, CdlGetlocP = 17, CdlGetTN = 19, CdlGetTD = 20, CdlSeekL = 21,
       CdlSeekP = 22, CdlReadS = 27 };
enum { CdlNoIntr = 0, CdlDataReady = 1, CdlComplete = 2, CdlAcknowledge = 3, CdlDataEnd = 4, CdlDiskError = 5 };
enum { CdlModeSpeed = 0x80, CdlModeRT = 0x40, CdlModeSize1 = 0x20, CdlModeSize0 = 0x10, CdlModeSF = 0x08 };

/* ---- anfitrion ---- */
int psx_cd_open(const char *path);
int psx_cd_open_fd(int fd);
void psx_cd_close(void);
/* sector crudo de 2352 bytes (con sincronia y cabecera; sintetizadas en imagenes .iso) */
int psx_cd_read_raw(uint32_t lba, uint8_t *raw);
/* sumidero de los sectores XA-ADPCM que reproduce ReadS (para libspu); 2352 bytes */
void psx_cd_set_xa_sink(void (*fn)(const uint8_t *raw));
/* comandos pendientes de terminar (sin avanzar el tiempo); devuelve cuantos entrego */
int psx_cd_service(void);
/* contadores para pruebas */
typedef struct { uint32_t commands, sectors_read, sectors_delivered, xa_sectors, callbacks; } PsxCdStats;
const PsxCdStats *psx_cd_stats(void);

/* ---- API de libcd que usa el juego ---- */
int CdInit(void);
int CdReset(int mode);
void CdFlush(void);
int CdSetDebug(int level);
int CdStatus(void);
int CdMode(void);
int CdLastCom(void);
CdlLOC *CdLastPos(void);
int CdSync(int mode, uint8_t *result);
int CdReady(int mode, uint8_t *result);
CdlCB CdSyncCallback(CdlCB func);
CdlCB CdReadyCallback(CdlCB func);
CdlCB CdDataCallback(CdlCB func);
int CdControl(uint8_t com, const uint8_t *param, uint8_t *result);
int CdControlF(uint8_t com, const uint8_t *param);
int CdControlB(uint8_t com, const uint8_t *param, uint8_t *result);
int CdMix(const CdlATV *vol);
int CdGetSector(void *madr, int size);
int CdGetSector2(void *madr, int size);
int CdDataSync(int mode);
CdlLOC *CdIntToPos(int i, CdlLOC *p);
int CdPosToInt(const CdlLOC *p);
CdlFILE *CdSearchFile(CdlFILE *fp, const char *name);
int CdRead(int sectors, uint32_t *buf, int mode);
int CdReadSync(int mode, uint8_t *result);
CdlCB CdReadCallback(CdlCB func);

#endif
