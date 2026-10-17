/* ACE.BIN desde el disco del usuario (etapa 7, paso 3). Ver psx_acebin.h. Mismo algoritmo y
 * mismas comprobaciones que tools/acebin.py (etapa 2), que es la referencia. */
#include <stdlib.h>
#include <string.h>

#include "psx_acebin.h"
#include "psx_libcd.h"

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t word_sum(const uint8_t *d, uint32_t nbytes) {
    uint32_t s = 0, i;
    for (i = 0; i + 4 <= nbytes; i += 4) s += le32(d + i);
    if (i < nbytes) {   /* ultima palabra incompleta: rellena con ceros */
        uint8_t t[4] = {0, 0, 0, 0};
        memcpy(t, d + i, nbytes - i);
        s += le32(t);
    }
    return s;
}

int psx_acebin_unpack(const uint8_t *raw, uint32_t raw_len, uint8_t *out, uint32_t max, PsxAceBin *info) {
    if (raw_len < 0x5C || memcmp(raw, "PS-Y", 4)) return PSX_ACEBIN_MAGIC;
    uint32_t end_addr = le32(raw + 0x08), file_size = le32(raw + 0x0C), size = le32(raw + 0x10);
    uint32_t data_off = le32(raw + 0x14) & ~3u, entry = le32(raw + 0x1C), load = le32(raw + 0x24);
    uint32_t checksum = le32(raw + 0x58);
    if (file_size > raw_len || load + size != end_addr || size > max || data_off >= file_size) return PSX_ACEBIN_HEADER;
    if (word_sum(raw, file_size) != 0) return PSX_ACEBIN_SUM;   /* (tamano+3)/4 palabras, la ultima con ceros */
    uint32_t i = data_off, o = 0;
    uint32_t nbits = raw[i++];
    if (nbits < 1 || nbits > 15) return PSX_ACEBIN_STREAM;
    uint32_t window = 1u << nbits, max_len = 1u << (16 - nbits);
    for (;;) {
        if (i >= file_size) return PSX_ACEBIN_STREAM;
        uint32_t flags = raw[i++];
        if (!flags) break;
        while (flags > 1) {
            uint32_t bit = flags & 1;
            flags >>= 1;
            if (bit) {
                if (i >= file_size || o >= size) return PSX_ACEBIN_STREAM;
                out[o++] = raw[i++];
            } else {
                if (i + 2 > file_size) return PSX_ACEBIN_STREAM;
                uint32_t v = (uint32_t)raw[i] << 8 | raw[i + 1];
                i += 2;
                uint32_t dist = (v & (window - 1)) ? (v & (window - 1)) : window;
                uint32_t len = (v >> nbits) ? (v >> nbits) : max_len;
                if (dist > o || o + len > size) return PSX_ACEBIN_STREAM;
                for (uint32_t k = 0; k < len; k++, o++) out[o] = out[o - dist];
            }
        }
    }
    if (o != size || file_size - i >= 4) return PSX_ACEBIN_STREAM;
    if (word_sum(out, size) != checksum) return PSX_ACEBIN_OUTSUM;
    if (info) { info->size = size; info->load = load; info->entry = entry; }
    return PSX_ACEBIN_OK;
}

int psx_acebin_from_disc(uint8_t *out, uint32_t max, PsxAceBin *info) {
    CdlFILE f;
    if (!CdSearchFile(&f, "\\ACE.BIN;1")) return PSX_ACEBIN_NOFILE;
    uint32_t lba = (uint32_t)CdPosToInt(&f.pos), n = (f.size + 2047) / 2048;
    uint8_t *raw = malloc((size_t)n * 2048), sec[2352];
    if (!raw) return PSX_ACEBIN_NOFILE;
    for (uint32_t k = 0; k < n; k++) {
        if (psx_cd_read_raw(lba + k, sec) < 0) { free(raw); return PSX_ACEBIN_NOFILE; }
        memcpy(raw + (size_t)k * 2048, sec + 24, 2048);   /* modo 2, forma 1: datos tras 24 bytes */
    }
    int r = psx_acebin_unpack(raw, f.size, out, max, info);
    free(raw);
    return r;
}
