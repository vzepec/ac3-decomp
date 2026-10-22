/* libcd sobre una imagen de disco (ver psx_libcd.h). */
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "psx_libcd.h"
#include "psx_libetc.h"
#include "psx_mem.h"

/* variables propias de la libcd original en la RAM del juego (las de la europea por defecto; cada
 * version pone las suyas con psx_cd_set_static). Etapa 8: con las de la europea en la americana, la
 * capa pisaba variables del juego (y el gestor del CD veia "tapa abierta" al acabar la Maze) */
static uint32_t SYNC_RESULT_ADDR = 0x800C5C98u;    /* resultado de CdSyncCallback */
static uint32_t READY_RESULT_ADDR = 0x800C5CA0u;   /* resultado de CdReadyCallback */
static uint32_t LASTPOS_ADDR = 0x800B40F4u;        /* CdlLOC de CdLastPos */
void psx_cd_set_static(uint32_t sync_result, uint32_t ready_result, uint32_t lastpos) {
    SYNC_RESULT_ADDR = sync_result; READY_RESULT_ADDR = ready_result; LASTPOS_ADDR = lastpos;
}
#define SUBSTEPS 8                      /* pasos de servicio del CD por fotograma */

static const uint8_t SYNC[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};

static struct {
    int fd, raw;                    /* raw: 2352 bytes por sector; si no, 2048 (ISO) */
    uint8_t status, mode, last_com, file, chan;
    uint32_t loc;                   /* destino del ultimo Setloc (LBA) */
    uint32_t cur;                   /* siguiente sector que entrega la lectura */
    int reading;                    /* 0, CdlReadN o CdlReadS */
    int acc;                        /* acumulador de sectores (en 1/(hz*SUBSTEPS) de segundo) */
    int pending;                    /* comando sin terminar: 0 o el comando */
    int sync_state;                 /* CdlComplete / CdlNoIntr */
    int ready_state;                /* CdlDataReady si hay un sector sin leer */
    int data_pending;               /* CdGetSector2 pendiente de su callback */
    CdlCB sync_cb, ready_cb, data_cb, read_cb;
    uint8_t sector[2352];           /* ultimo sector crudo */
    uint32_t sector_lba;
    int rpos;                       /* posicion de lectura de CdGetSector dentro del sector */
    CdlATV mix;
    void (*xa_sink)(const uint8_t *raw);
    PsxCdStats st;
} cd = {.fd = -1};

static uint8_t bcd(int v) { return (uint8_t)((v / 10) * 16 + v % 10); }

/* ---- imagen de disco ---- */
static int detect(void) {
    uint8_t b[12];
    if (pread(cd.fd, b, 12, 16 * 2352) == 12 && !memcmp(b, SYNC, 12)) { cd.raw = 1; return 0; }
    cd.raw = 0;
    return 0;
}

int psx_cd_open_fd(int fd) {
    psx_cd_close();
    cd.fd = fd;
    return fd < 0 ? -1 : detect();
}

int psx_cd_open(const char *path) { return psx_cd_open_fd(open(path, O_RDONLY)); }

void psx_cd_close(void) {
    if (cd.fd >= 0) close(cd.fd);
    cd.fd = -1;
}

int psx_cd_read_raw(uint32_t lba, uint8_t *raw) {
    if (cd.fd < 0) return -1;
    if (cd.raw) return pread(cd.fd, raw, 2352, (off_t)lba * 2352) == 2352 ? 0 : -1;
    /* imagen de 2048: se sintetizan sincronia, cabecera (modo 2) y subcabecera de datos */
    memset(raw, 0, 2352);
    memcpy(raw, SYNC, 12);
    uint32_t a = lba + 150;
    raw[12] = bcd(a / 4500); raw[13] = bcd(a / 75 % 60); raw[14] = bcd(a % 75); raw[15] = 2;
    raw[18] = raw[22] = 0x08;
    return pread(cd.fd, raw + 24, 2048, (off_t)lba * 2048) == 2048 ? 0 : -1;
}

void psx_cd_set_xa_sink(void (*fn)(const uint8_t *raw)) { cd.xa_sink = fn; }
const PsxCdStats *psx_cd_stats(void) { return &cd.st; }

/* ---- controlador ---- */
static uint8_t *sync_result(void) { return psx_ptr(SYNC_RESULT_ADDR); }
static uint8_t *ready_result(void) { return psx_ptr(READY_RESULT_ADDR); }

static void fire(CdlCB cb, int intr, uint32_t result_addr) {
    if (!cb) return;
    cd.st.callbacks++;
    psx_call(cb, (uint32_t)intr, result_addr);
}

/* entrega la terminacion del comando pendiente */
static int complete(void) {
    if (!cd.pending) return 0;
    cd.pending = 0;
    cd.sync_state = CdlComplete;
    memset(sync_result(), 0, 8);
    sync_result()[0] = cd.status;
    fire(cd.sync_cb, CdlComplete, SYNC_RESULT_ADDR);
    return 1;
}

int psx_cd_service(void) {
    int n = complete();
    if (cd.data_pending) {
        cd.data_pending = 0;
        if (cd.data_cb) { cd.st.callbacks++; psx_call(cd.data_cb, 0, 0); }
        n++;
    }
    return n;
}

/* un sector de la lectura en curso */
static void sector_event(void) {
    if (psx_cd_read_raw(cd.cur, cd.sector) < 0) { cd.reading = 0; cd.status = 0x02; return; }
    cd.sector_lba = cd.cur++;
    cd.st.sectors_read++;
    uint8_t file = cd.sector[16], chan = cd.sector[17], sub = cd.sector[18];
    /* XA-ADPCM en tiempo real: los sectores de audio no llegan al programa */
    if ((cd.mode & CdlModeRT) && cd.sector[15] == 2 && (sub & 0x04)) {
        if (!(cd.mode & CdlModeSF) || (file == cd.file && chan == cd.chan)) {
            cd.st.xa_sectors++;
            if (cd.xa_sink) cd.xa_sink(cd.sector);
        }
        return;
    }
    cd.rpos = 0;
    cd.ready_state = CdlDataReady;
    cd.st.sectors_delivered++;
    memset(ready_result(), 0, 8);
    ready_result()[0] = cd.status;
    fire(cd.ready_cb, CdlDataReady, READY_RESULT_ADDR);
}

void psx_cd_frame(void) {
    int hz = psx_refresh();
    for (int s = 0; s < SUBSTEPS; s++) {
        psx_cd_service();
        if (!cd.reading) { cd.acc = 0; continue; }
        cd.acc += (cd.mode & CdlModeSpeed) ? 150 : 75;
        while (cd.reading && cd.acc >= hz * SUBSTEPS) {
            cd.acc -= hz * SUBSTEPS;
            sector_event();
        }
    }
}

static void issue(uint8_t com, const uint8_t *param, uint8_t *result) {
    cd.st.commands++;
    cd.last_com = com;
    uint8_t res[8] = {0};
    switch (com) {
    case CdlSetloc: {
        CdlLOC *lp = psx_ptr(LASTPOS_ADDR);
        memcpy(lp, param, 3);
        cd.loc = (uint32_t)CdPosToInt(lp);
        break;
    }
    case CdlSetmode: cd.mode = param[0]; break;
    case CdlSetfilter: cd.file = param[0]; cd.chan = param[1]; break;
    case CdlReadN: case CdlReadS: case CdlSeekL: case CdlSeekP:
        cd.cur = cd.loc;
        cd.reading = (com == CdlReadN || com == CdlReadS) ? com : 0;
        cd.status = cd.reading ? 0x22 : 0x02;
        cd.acc = 0;
        break;
    case CdlPause: case CdlStop: case CdlStandby:
        cd.reading = 0;
        cd.status = com == CdlStop ? 0x00 : 0x02;
        break;
    case CdlGetlocL:
        memcpy(res, cd.sector + 12, 8);
        break;
    case CdlGetlocP: {
        uint32_t a = cd.sector_lba + 150;
        res[0] = 1; res[1] = 1;
        res[2] = bcd(cd.sector_lba / 4500); res[3] = bcd(cd.sector_lba / 75 % 60); res[4] = bcd(cd.sector_lba % 75);
        res[5] = bcd(a / 4500); res[6] = bcd(a / 75 % 60); res[7] = bcd(a % 75);
        break;
    }
    case CdlGetTN: res[1] = 1; res[2] = 1; break;
    default: break;
    }
    if (com != CdlGetlocL && com != CdlGetlocP) res[0] = cd.status;
    if (result) memcpy(result, res, com == CdlGetlocL || com == CdlGetlocP ? 8 : (com == CdlGetTN ? 3 : 1));
    cd.pending = com;
    cd.sync_state = CdlNoIntr;
}

/* ---- API ---- */
int CdInit(void) {
    int fd = cd.fd;
    void (*sink)(const uint8_t *) = cd.xa_sink;
    int raw = cd.raw;
    memset(&cd, 0, sizeof cd);
    cd.fd = fd; cd.raw = raw; cd.xa_sink = sink;
    cd.status = 0x02;
    cd.sync_state = CdlComplete;
    return fd >= 0;
}

int CdReset(int mode) { cd.reading = 0; cd.pending = 0; cd.sync_state = CdlComplete; cd.status = 0x02; return 1; }
void CdFlush(void) {}
int CdSetDebug(int level) { return 0; }
int CdStatus(void) { return cd.status; }
int CdMode(void) { return cd.mode; }
int CdLastCom(void) { return cd.last_com; }
CdlLOC *CdLastPos(void) { return psx_ptr(LASTPOS_ADDR); }

int CdSync(int mode, uint8_t *result) {
    if (mode == 0) while (cd.pending) psx_cd_service();
    if (result) memcpy(result, sync_result(), 8);
    return cd.pending ? CdlNoIntr : cd.sync_state;
}

int CdReady(int mode, uint8_t *result) {
    if (mode == 0) while (cd.reading && cd.ready_state != CdlDataReady) psx_vblank();
    if (result) memcpy(result, ready_result(), 8);
    int r = cd.ready_state;
    cd.ready_state = CdlNoIntr;
    return r;
}

CdlCB CdSyncCallback(CdlCB func) { CdlCB o = cd.sync_cb; cd.sync_cb = func; return o; }
CdlCB CdReadyCallback(CdlCB func) { CdlCB o = cd.ready_cb; cd.ready_cb = func; return o; }
CdlCB CdDataCallback(CdlCB func) { CdlCB o = cd.data_cb; cd.data_cb = func; return o; }
CdlCB CdReadCallback(CdlCB func) { CdlCB o = cd.read_cb; cd.read_cb = func; return o; }

int CdControl(uint8_t com, const uint8_t *param, uint8_t *result) { issue(com, param, result); return 1; }
int CdControlF(uint8_t com, const uint8_t *param) { issue(com, param, NULL); return 1; }
int CdControlB(uint8_t com, const uint8_t *param, uint8_t *result) {
    issue(com, param, result);
    while (cd.pending) psx_cd_service();
    return 1;
}

int CdMix(const CdlATV *vol) { cd.mix = *vol; return 1; }

/* datos del sector segun el modo: 2340 bytes desde la cabecera, o 2048 desde los datos */
static const uint8_t *sector_data(void) {
    if (cd.mode & CdlModeSize1) return cd.sector + 12;
    return cd.sector + 24;
}

int CdGetSector(void *madr, int size) {
    int n = size * 4, max = (cd.mode & CdlModeSize1) ? 2340 : 2048;
    if (cd.rpos + n > max) n = max - cd.rpos;
    if (n > 0) memcpy(madr, sector_data() + cd.rpos, (size_t)n);
    cd.rpos += size * 4;
    return 1;
}

int CdGetSector2(void *madr, int size) {
    CdGetSector(madr, size);
    cd.data_pending = 1;
    return 1;
}

int CdDataSync(int mode) { return 0; }

/* Mismas operaciones que la original (0x800A4438 en EU): division con signo y BCD
 * por (x/10)*16 + x%10; el byte de pista no se toca. Comparadas en tools/psxcheck_io.py. */
CdlLOC *CdIntToPos(int i, CdlLOC *p) {
    i += 150;
    int s = i / 75, m = s / 60;
    p->sector = (uint8_t)((i - s * 75) / 10 * 16 + (i - s * 75) % 10);
    p->second = (uint8_t)((s - m * 60) / 10 * 16 + (s - m * 60) % 10);
    p->minute = (uint8_t)(m / 10 * 16 + m % 10);
    return p;
}

int CdPosToInt(const CdlLOC *p) {
    return ((p->minute >> 4) * 10 + (p->minute & 15)) * 4500 + ((p->second >> 4) * 10 + (p->second & 15)) * 75 +
           (p->sector >> 4) * 10 + (p->sector & 15) - 150;
}

/* ---- ISO9660 ---- */
static int rd_sector2048(uint32_t lba, uint8_t *out) {
    uint8_t raw[2352];
    if (psx_cd_read_raw(lba, raw) < 0) return -1;
    memcpy(out, raw + 24, 2048);
    return 0;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* busca `name` (sin ';1') en el directorio (lba, size); devuelve el registro */
static int dir_find(uint32_t lba, uint32_t size, const char *name, size_t nlen, uint32_t *out_lba,
                    uint32_t *out_size, int *is_dir) {
    uint8_t b[2048];
    for (uint32_t off = 0; off < size; off += 2048) {
        if (rd_sector2048(lba + off / 2048, b) < 0) return -1;
        for (int p = 0; p < 2048 && b[p];) {
            int len = b[p], fl = b[p + 32];
            const char *fn = (const char *)b + p + 33;
            int fnl = fl;
            for (int k = 0; k < fl; k++) if (fn[k] == ';') { fnl = k; break; }
            if (fnl > 0 && fn[fnl - 1] == '.') fnl--;
            if ((size_t)fnl == nlen) {
                int eq = 1;
                for (size_t k = 0; k < nlen && eq; k++) eq = toupper((unsigned char)fn[k]) == toupper((unsigned char)name[k]);
                if (eq) {
                    *out_lba = le32(b + p + 2);
                    *out_size = le32(b + p + 10);
                    *is_dir = (b[p + 25] & 2) != 0;
                    return 0;
                }
            }
            p += len;
        }
    }
    return -1;
}

/* nombre con el formato de PsyQ: "\\DIR\\ARCHIVO.EXT;1" */
CdlFILE *CdSearchFile(CdlFILE *fp, const char *name) {
    uint8_t pvd[2048];
    if (rd_sector2048(16, pvd) < 0 || memcmp(pvd + 1, "CD001", 5)) return NULL;
    uint32_t lba = le32(pvd + 156 + 2), size = le32(pvd + 156 + 10);
    int is_dir = 1;
    const char *p = name;
    while (*p == '\\' || *p == '/') p++;
    const char *base = p;
    while (*p) {
        const char *e = p;
        while (*e && *e != '\\' && *e != '/' && *e != ';') e++;
        if (!is_dir || dir_find(lba, size, p, (size_t)(e - p), &lba, &size, &is_dir) < 0) return NULL;
        base = p;
        if (*e == ';') { while (*e && *e != '\\' && *e != '/') e++; }
        p = e;
        while (*p == '\\' || *p == '/') p++;
    }
    CdIntToPos((int)lba, &fp->pos);
    fp->pos.track = 0;
    fp->size = size;
    memset(fp->name, 0, sizeof fp->name);
    size_t n = strlen(base);
    memcpy(fp->name, base, n < 15 ? n : 15);
    return fp;
}

/* CdRead: lee `sectors` sectores desde la ultima posicion de Setloc (sincrono) */
int CdRead(int sectors, uint32_t *buf, int mode) {
    uint8_t m = (uint8_t)mode;
    issue(CdlSetmode, &m, NULL);
    psx_cd_service();
    int sz = (cd.mode & CdlModeSize1) ? 2340 : 2048;
    uint8_t *dst = (uint8_t *)buf;
    for (int i = 0; i < sectors; i++) {
        if (psx_cd_read_raw(cd.loc + (uint32_t)i, cd.sector) < 0) return 0;
        cd.sector_lba = cd.loc + (uint32_t)i;
        memcpy(dst + (size_t)i * (size_t)sz, sector_data(), (size_t)sz);
        cd.st.sectors_read++;
    }
    cd.loc += (uint32_t)sectors;
    cd.status = 0x02;
    if (cd.read_cb) fire(cd.read_cb, CdlComplete, SYNC_RESULT_ADDR);
    return 1;
}

int CdReadSync(int mode, uint8_t *result) {
    if (result) result[0] = cd.status;
    return 0;
}
