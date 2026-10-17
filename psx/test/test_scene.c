/* Prueba de integracion de la capa PsyQ con datos reales: dibuja el terreno de una mision
 * con libgte (GTE emulada) y libgpu (GPU por software), como lo haria el juego:
 * texturas con LoadImage, vertices con RTPT, primitivas POLY_FT4/GT4 construidas desde las
 * plantillas de los modelos, NCLIP, tabla de ordenacion y DrawOTag. Escribe el framebuffer
 * (320x240 en la VRAM) como PNG sin comprimir.
 *   psx/build/test_scene <imagen.bin> <mision> <salida.ppm> [x y z rumbo cabeceo]
 * Lectura del disco: android/src/ace.c (port de las herramientas de tools/). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ace.h"
#include "psx_display.h"
#include "psx_gpu.h"
#include "psx_gte.h"
#include "psx_libgpu.h"
#include "psx_libgte.h"
#include "psx_mem.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

static uint8_t *section(const Record *r, int k, uint32_t *len) {
    uint32_t n;
    const uint8_t *raw = record_section(r, k, &n);
    if (!raw || !n) return NULL;
    if (is_ulz(raw)) return ulz_decompress(raw, n, len);
    uint8_t *c = malloc(n);
    memcpy(c, raw, n);
    *len = n;
    return c;
}

/* TIM -> VRAM con LoadImage (los datos se copian a la RAM de la PS1, como en el juego) */
static int upload_tims(const uint8_t *b, uint32_t len) {
    int n = 0;
    for (uint32_t off = 0; off + 8 <= len; off += 4) {
        uint32_t flags = rd32(b + off + 4);
        if (rd32(b + off) != 0x10 || (flags & ~0xFu) || (flags & 7) > 3) continue;
        uint32_t p = off + 8;
        int ok = 1;
        for (int blk = (flags & 8) ? 0 : 1; blk < 2 && ok; blk++) {
            if (p + 12 > len) { ok = 0; break; }
            uint32_t bn = rd32(b + p);
            RECT r = {(int16_t)rd16(b + p + 4), (int16_t)rd16(b + p + 6), (int16_t)rd16(b + p + 8), (int16_t)rd16(b + p + 10)};
            if (!r.w || !r.h || bn != 12u + 2u * r.w * r.h || p + bn > len || r.x + r.w > 1024 || r.y + r.h > 512) { ok = 0; break; }
            memcpy(psx_ram + 0x180000, b + p + 12, bn - 12);
            LoadImage(&r, (uint32_t *)(psx_ram + 0x180000));
            p += bn;
        }
        if (ok) { n++; off = p - 4; }
    }
    return n;
}

#define OT_LEN 1024
#define VIEW 7

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "uso: %s <imagen> <mision> <salida.ppm> [x y z rumbo cabeceo]\n", argv[0]); return 2; }
    Disc d; AcePack pk;
    if (disc_open(&d, argv[1]) || pack_open(&d, &pk)) { puts("disco"); return 1; }
    uint32_t size, len;
    uint8_t *raw = pack_read_record(&d, &pk, (uint32_t)atoi(argv[2]), &size);
    Record r;
    if (!raw || record_parse(&r, raw, size)) { puts("registro"); return 1; }
    ResetGraph(0);
    int tims = 0;
    for (int k = 10; k <= 14; k++) { uint8_t *t = section(&r, k, &len); if (t) { tims += upload_tims(t, len); free(t); } }
    uint8_t *tm = section(&r, 0, &len), *tl = section(&r, 1, &len);
    uint32_t tl_len = len;
    static uint32_t st[512], en[512];
    uint32_t ntiles = offset_table(tl, tl_len, st, en, 511);
    /* camara (coordenadas PS1: y hacia abajo); por defecto, el inicio de la mision 6 */
    double cx = argc > 4 ? atof(argv[4]) : 48000, cy = argc > 5 ? atof(argv[5]) : -2600, cz = argc > 6 ? atof(argv[6]) : -48000;
    int yaw = argc > 7 ? atoi(argv[7]) : 0xE00, pitch = argc > 8 ? atoi(argv[8]) : 0x120;   /* 4096 = 360 grados */
    /* matriz de vista = traspuesta de la orientacion de la camara */
    SVECTOR ang = {(int16_t)pitch, (int16_t)yaw, 0, 0};
    MATRIX cam, view;
    RotMatrixYXZ(&ang, &cam);
    TransposeMatrix(&cam, &view);
    PsxGte *g = psx_gte_get_context();
    psx_gte_reset(g);
    InitGeom();
    psx_gte_write_ctrl(g, 24, 160u << 16);   /* OFX, OFY: centro de una pantalla de 320x240 */
    psx_gte_write_ctrl(g, 25, 120u << 16);
    psx_gte_write_ctrl(g, 26, 220);          /* H */
    /* pantalla y tabla de ordenacion en la RAM de la PS1 */
    DRAWENV de;
    SetDefDrawEnv(&de, 0, 0, 320, 240);
    de.isbg = 1; de.r0 = 90; de.g0 = 110; de.b0 = 140;
    PutDrawEnv(&de);
    uint32_t *ot = (uint32_t *)(psx_ram + 0x100000);
    ClearOTagR(ot, OT_LEN);
    uint8_t *pk_next = psx_ram + 0x110000;
    int prims = 0, culled = 0;
    int ccx = (int)floor(cx / 2048) + 128, ccz = (int)floor(cz / 2048) + 128;
    static int32_t sxy[256], sz[256];
    for (int tz = ccz - VIEW; tz <= ccz + VIEW; tz++)
        for (int tx = ccx - VIEW; tx <= ccx + VIEW; tx++) {
            if (tx < 0 || tx > 255 || tz < 0 || tz > 255) continue;
            uint32_t t = rd16(tm + 2 * (tz * 256 + tx)) & 0x1FF;
            if (!t || t > ntiles) continue;
            const uint8_t *m = tl + st[t - 1];
            uint32_t ml = en[t - 1] - st[t - 1];
            uint32_t ov = rd32(m + 4), op = rd32(m + 12), oe = rd32(m + 16);
            /* traslacion: vista * (centro de la celda - camara) */
            VECTOR rel = {(int32_t)((tx - 128) * 2048 + 1024 - cx), (int32_t)(0 - cy), (int32_t)((tz - 128) * 2048 + 1024 - cz), 0};
            VECTOR tr;
            ApplyMatrixLV(&view, &rel, &tr);
            MATRIX vm = view;
            vm.t[0] = tr.vx; vm.t[1] = tr.vy; vm.t[2] = tr.vz;
            if (tr.vz < -2048) continue;
            SetRotMatrix(&vm);
            SetTransMatrix(&vm);
            /* vertices de 3 en 3 con RTPT (x0,y0,x1,y1,x2,y2,z0,z1,z2,pad) */
            uint32_t nv = rd32(m + ov);
            for (uint32_t gi = 0; gi * 3 < nv; gi++) {
                const uint8_t *q = m + ov + 4 + 20 * gi;
                for (int k = 0; k < 3; k++) {
                    psx_gte_write_data(g, 2 * k, rd32(q + 4 * k));
                    psx_gte_write_data(g, 2 * k + 1, (uint32_t)(int32_t)(int16_t)rd16(q + 12 + 2 * k));
                }
                psx_gte_command(g, 0x0280030);   /* RTPT */
                for (int k = 0; k < 3; k++) {
                    sxy[3 * gi + k] = (int32_t)psx_gte_read_data(g, 12 + k);
                    sz[3 * gi + k] = (int32_t)psx_gte_read_data(g, 17 + k);
                }
            }
            /* primitivas: POLY_FT4 (36 B, un color) y POLY_GT4 (48 B, cuatro colores) */
            for (int blk = 0; blk < 2; blk++) {
                uint32_t off = blk ? oe : op, psz = blk ? 48 : 36, np = rd32(m + off);
                for (uint32_t j = 0; j < np && off + 4 + psz * (j + 1) <= ml; j++) {
                    const uint8_t *q = m + off + 4 + psz * j;
                    int i0 = q[16], i1 = q[17], i2 = q[18], i3 = q[19];
                    if (i0 >= (int)nv || i1 >= (int)nv || i2 >= (int)nv || i3 >= (int)nv) continue;
                    if (!sz[i0] || !sz[i1] || !sz[i2] || !sz[i3]) continue;
                    /* NCLIP sobre (0,1,2) como el juego */
                    psx_gte_write_data(g, 12, (uint32_t)sxy[i0]);
                    psx_gte_write_data(g, 13, (uint32_t)sxy[i1]);
                    psx_gte_write_data(g, 14, (uint32_t)sxy[i2]);
                    psx_gte_command(g, 0x1400006);
                    if ((int32_t)psx_gte_read_data(g, 24) <= 0) {
                        psx_gte_write_data(g, 12, (uint32_t)sxy[i1]);
                        psx_gte_write_data(g, 13, (uint32_t)sxy[i3]);
                        psx_gte_write_data(g, 14, (uint32_t)sxy[i2]);
                        psx_gte_command(g, 0x1400006);
                        if ((int32_t)psx_gte_read_data(g, 24) <= 0) { culled++; continue; }
                    }
                    int zmax = sz[i0];
                    if (sz[i1] > zmax) zmax = sz[i1];
                    if (sz[i2] > zmax) zmax = sz[i2];
                    if (sz[i3] > zmax) zmax = sz[i3];
                    int oz = zmax >> 5;
                    if (oz >= OT_LEN) continue;
                    uint32_t *p = (uint32_t *)pk_next;
                    if (!blk) {   /* POLY_FT4: tag, rgbc, xy0, uv0+clut, xy1, uv1+tpage, xy2, uv2, xy3, uv3 */
                        p[1] = (rd32(q + 32) & 0xFFFFFF) | 0x2C000000u;
                        p[2] = (uint32_t)sxy[i0]; p[3] = rd32(q + 20);
                        p[4] = (uint32_t)sxy[i1]; p[5] = rd32(q + 24);
                        p[6] = (uint32_t)sxy[i2]; p[7] = rd16(q + 28);
                        p[8] = (uint32_t)sxy[i3]; p[9] = rd16(q + 30);
                        p[0] = (9u << 24) | (ot[oz] & 0xFFFFFF);
                        pk_next += 40;
                    } else {      /* POLY_GT4: tag, rgb0+code, xy0, uv0+clut, rgb1, xy1, uv1+tpage, ... */
                        p[1] = (rd32(q + 32) & 0xFFFFFF) | 0x3C000000u;
                        p[2] = (uint32_t)sxy[i0]; p[3] = rd32(q + 20);
                        p[4] = rd32(q + 36) & 0xFFFFFF; p[5] = (uint32_t)sxy[i1]; p[6] = rd32(q + 24);
                        p[7] = rd32(q + 40) & 0xFFFFFF; p[8] = (uint32_t)sxy[i2]; p[9] = rd16(q + 28);
                        p[10] = rd32(q + 44) & 0xFFFFFF; p[11] = (uint32_t)sxy[i3]; p[12] = rd16(q + 30);
                        p[0] = (12u << 24) | (ot[oz] & 0xFFFFFF);
                        pk_next += 52;
                    }
                    ot[oz] = psx_addr(p) & 0xFFFFFF;
                    prims++;
                }
            }
        }
    DrawOTag(&ot[OT_LEN - 1]);
    PsxGpu *gpu = psx_gpu();
    printf("TIM %d, primitivas %d (%d descartadas por NCLIP), pixeles %u, paquetes %ld B\n", tims, prims, culled,
           gpu->pixels_drawn, (long)(pk_next - (psx_ram + 0x110000)));
    /* salida por el camino de video de la capa (psx_display.h), como la vera la pantalla */
    DISPENV disp;
    SetDefDispEnv(&disp, 0, 0, 320, 240);
    PutDispEnv(&disp);
    SetDispMask(1);
    static uint32_t img[320 * 240];
    int iw, ih;
    psx_display_frame(img, 320 * 240, &iw, &ih);
    FILE *f = fopen(argv[3], "wb");
    fprintf(f, "P6\n%d %d\n255\n", iw, ih);
    for (int i = 0; i < iw * ih; i++) fwrite(&img[i], 1, 3, f);
    fclose(f);
    return 0;
}
