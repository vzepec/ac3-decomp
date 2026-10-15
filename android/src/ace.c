/* Port a C de las herramientas de lectura de tools/ (ver ace.h). */
#include "ace.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const uint8_t SYNC[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int disc_detect(Disc *d);

int disc_open(Disc *d, const char *path) {
    d->f = fopen(path, "rb");
    if (!d->f) return -1;
    return disc_detect(d);
}

int disc_open_fd(Disc *d, int fd) {
    d->f = fdopen(fd, "rb");
    if (!d->f) return -1;
    return disc_detect(d);
}

static int disc_detect(Disc *d) {
    uint8_t head[16];
    if (fread(head, 1, 16, d->f) != 16) { fclose(d->f); d->f = NULL; return -1; }
    if (memcmp(head, SYNC, 12) == 0) {
        d->sector_size = 2352;
        d->data_offset = head[15] == 1 ? 16 : 24;
    } else {
        d->sector_size = 2048;
        d->data_offset = 0;
    }
    return 0;
}

void disc_close(Disc *d) {
    if (d->f) fclose(d->f);
    d->f = NULL;
}

int disc_read(Disc *d, uint32_t lba, void *buf, uint32_t nbytes) {
    uint8_t *out = buf;
    while (nbytes) {
        uint32_t n = nbytes < 2048 ? nbytes : 2048;
        if (fseeko(d->f, (off_t)lba * d->sector_size + d->data_offset, SEEK_SET)) return -1;
        if (fread(out, 1, n, d->f) != n) return -1;
        out += n;
        nbytes -= n;
        lba++;
    }
    return 0;
}

int disc_volume_is_ac3(Disc *d) {
    uint8_t pvd[2048];
    if (disc_read(d, 16, pvd, 2048)) return 0;
    return memcmp(pvd + 1, "CD001", 5) == 0 && memcmp(pvd + 40, "ACE3ES", 6) == 0;
}

/* Busca un archivo en el directorio raiz (los de AC3 estan todos ahi). */
int disc_find(Disc *d, const char *name, DiscFile *out) {
    uint8_t pvd[2048], dir[2048];
    if (disc_read(d, 16, pvd, 2048)) return -1;
    uint32_t rlba = rd32(pvd + 156 + 2), rsize = rd32(pvd + 156 + 10);
    size_t nlen = strlen(name);
    for (uint32_t s = 0; s < (rsize + 2047) / 2048; s++) {
        if (disc_read(d, rlba + s, dir, 2048)) return -1;
        uint32_t o = 0;
        while (o < 2048 && dir[o]) {
            uint8_t len = dir[o], fl = dir[o + 32];
            const char *fn = (const char *)dir + o + 33;
            if (fl >= nlen && strncasecmp(fn, name, nlen) == 0 && (fl == nlen || fn[nlen] == ';')) {
                out->lba = rd32(dir + o + 2);
                out->size = rd32(dir + o + 10);
                return 0;
            }
            o += len;
        }
    }
    return -1;
}

int pack_open(Disc *d, AcePack *p) {
    DiscFile idx;
    memset(p, 0, sizeof *p);
    if (disc_find(d, "ACE.BPH", &idx) || disc_find(d, "ACE.BPB", &p->pack)) return -1;
    uint8_t *raw = malloc(idx.size);
    if (!raw || disc_read(d, idx.lba, raw, idx.size)) { free(raw); return -1; }
    if (memcmp(raw, "AC3E", 4) != 0) { free(raw); return -1; }
    p->count = rd32(raw + 0x10);
    if (0x14 + 8 * p->count != idx.size) { free(raw); return -1; }
    p->tag = malloc(4 * p->count);
    p->sector = malloc(4 * p->count);
    for (uint32_t i = 0; i < p->count; i++) {
        p->tag[i] = rd32(raw + 0x14 + 8 * i);
        p->sector[i] = rd32(raw + 0x18 + 8 * i);
    }
    free(raw);
    return 0;
}

void pack_free(AcePack *p) {
    free(p->tag);
    free(p->sector);
    memset(p, 0, sizeof *p);
}

uint8_t *pack_read_record(Disc *d, AcePack *p, uint32_t i, uint32_t *size) {
    if (i >= p->count) return NULL;
    uint32_t n = (p->tag[i] & 0xFFFFFF) * 4;
    uint8_t *buf = malloc(n + 64);
    if (!buf) return NULL;
    memset(buf + n, 0, 64);
    if (disc_read(d, p->pack.lba + p->sector[i], buf, n)) { free(buf); return NULL; }
    *size = n;
    return buf;
}

int is_ulz(const uint8_t *p) { return p[0] == 'U' && p[1] == 'l' && p[2] == 'z' && p[3] == 0x1A; }

/* tools/ulz.py: variante 2 (32 flags por palabra, para por tamano) y variante 0
 * (bit centinela en cada palabra de flags; palabra 0 = fin). */
uint8_t *ulz_decompress(const uint8_t *buf, size_t len, uint32_t *out_size) {
    if (len < 16 || !is_ulz(buf)) return NULL;
    uint32_t a = rd32(buf + 4), b = rd32(buf + 8), c = rd32(buf + 12);
    uint32_t size = a & 0xFFFFFF, variant = a >> 24, lit = b & 0xFFFFFF, nbits = b >> 24, ref = c;
    if ((variant != 0 && variant != 2) || lit > len || ref > len) return NULL;
    uint8_t *out = malloc(size + 16);
    if (!out) return NULL;
    uint32_t mask = (1u << nbits) - 1, o = 0;
    size_t fp = 16, lp = lit, rp = ref;
    if (variant == 2) {
        uint32_t flags = 0;
        int cnt = 0;
        while (o < size) {
            if (cnt == 0) {
                if (fp + 4 > len) goto bad;
                flags = rd32(buf + fp); fp += 4; cnt = 32;
            }
            uint32_t bit = flags >> 31;
            flags <<= 1; cnt--;
            if (bit) {
                if (lp >= len) goto bad;
                out[o++] = buf[lp++];
            } else {
                if (rp + 2 > len) goto bad;
                uint32_t v = buf[rp] | buf[rp + 1] << 8; rp += 2;
                uint32_t dist = (v & mask) + 1, n = (v >> nbits) + 3;
                if (dist > o) goto bad;
                while (n-- && o < size) { out[o] = out[o - dist]; o++; }
            }
        }
    } else {
        for (;;) {
            if (fp + 4 > len) goto bad;
            uint32_t flags = rd32(buf + fp); fp += 4;
            if (!flags) break;
            for (;;) {
                uint32_t bit = flags >> 31;
                flags <<= 1;
                if (!flags) break;
                if (bit) {
                    if (lp >= len || o >= size) goto bad;
                    out[o++] = buf[lp++];
                } else {
                    if (rp + 2 > len) goto bad;
                    uint32_t v = buf[rp] | buf[rp + 1] << 8; rp += 2;
                    uint32_t dist = (v & mask) + 1, n = (v >> nbits) + 3;
                    if (dist > o || o + n > size) goto bad;
                    while (n--) { out[o] = out[o - dist]; o++; }
                }
            }
        }
        if (o != size) goto bad;
    }
    *out_size = size;
    return out;
bad:
    free(out);
    return NULL;
}

uint32_t offset_table(const uint8_t *buf, uint32_t len, uint32_t *starts, uint32_t *ends, uint32_t max) {
    if (len < 8) return 0;
    uint32_t n = rd32(buf);
    if (n == 0 || 4 + 4ull * n > len || rd32(buf + 4) != 4 + 4 * n) return 0;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; i++) {
        starts[i] = rd32(buf + 4 + 4 * i);
        ends[i] = (i + 1 < n) ? rd32(buf + 8 + 4 * i) : len;
        if (starts[i] > ends[i] || ends[i] > len) return 0;
    }
    return n;
}

int record_parse(Record *r, const uint8_t *raw, uint32_t size) {
    memset(r, 0, sizeof *r);
    r->raw = raw;
    r->size = size;
    r->type = rd32(raw);
    uint32_t first = rd32(raw + 4);          /* offset del preludio = fin de cabecera */
    if (first < 8 || first % 4 || first > 4 + 4 * 64) return -1;
    uint32_t n = (first - 4) / 4;            /* entradas de offsets, preludio incluido */
    for (uint32_t i = 0; i < n; i++) {
        r->off[i] = rd32(raw + 4 + 4 * i);
        if (r->off[i] > size || (i && r->off[i] < r->off[i - 1])) return -1;
    }
    r->nsec = n - 1;
    r->off[n] = size;
    return 0;
}

const uint8_t *record_section(const Record *r, int k, uint32_t *len) {
    int i = k + 1;                           /* -1 -> preludio */
    if (i < 0 || (uint32_t)i > r->nsec) return NULL;
    *len = r->off[i + 1] - r->off[i];
    return r->raw + r->off[i];
}
