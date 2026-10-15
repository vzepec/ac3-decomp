/* Construccion de la escena a partir de los datos del disco.
 * Formatos: docs/04-formatos.md (Model, LodModel iluminado, TIM, mapa de teselas). */
#include "scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }

static void mesh_push(Mesh *m, const Vtx *v) {
    if (m->n == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 4096;
        m->v = realloc(m->v, m->cap * sizeof(Vtx));
    }
    m->v[m->n++] = *v;
}

void scene_init(Scene *s) { memset(s, 0, sizeof *s); }

void scene_free(Scene *s) {
    free(s->tiles.v);
    free(s->plane.v);
    memset(s, 0, sizeof *s);
}

/* ---- TIM -> VRAM (tools/tim2png.py) ---- */

static int tim_block(const uint8_t *b, uint32_t len, uint32_t off, uint32_t *bnum, uint16_t r[4]) {
    if (off + 12 > len) return 0;
    *bnum = rd32(b + off);
    for (int i = 0; i < 4; i++) r[i] = rd16(b + off + 4 + 2 * i);
    if (!r[2] || !r[3] || *bnum != 12u + 2u * r[2] * r[3] || off + *bnum > len) return 0;
    if (r[0] + r[2] > VRAM_W || r[1] + r[3] > VRAM_H) return 0;
    return 1;
}

static void vram_put(Scene *s, const uint8_t *src, const uint16_t r[4]) {
    for (int y = 0; y < r[3]; y++)
        for (int x = 0; x < r[2]; x++)
            s->vram[(r[1] + y) * VRAM_W + r[0] + x] = rd16(src + 2 * (y * r[2] + x));
}

/* Sube a la VRAM todas las TIM validas del buffer (alineadas a 4). */
static int tims_upload(Scene *s, const uint8_t *b, uint32_t len) {
    int n = 0;
    uint32_t off = 0;
    while (off + 8 <= len) {
        uint32_t flags = rd32(b + off + 4);
        if (rd32(b + off) == 0x10 && !(flags & ~0xFu) && (flags & 7) <= 3) {
            uint32_t p = off + 8, bnum;
            uint16_t cr[4], ir[4];
            int ok = 1;
            if (flags & 8) {
                ok = tim_block(b, len, p, &bnum, cr);
                if (ok) { vram_put(s, b + p + 12, cr); p += bnum; }
            }
            if (ok && tim_block(b, len, p, &bnum, ir)) {
                vram_put(s, b + p + 12, ir);
                off = p + bnum;
                n++;
                continue;
            }
        }
        off += 4;
    }
    s->tims_loaded += n;
    return n;
}

/* Descomprime una seccion ULZ (o la devuelve copiada si va en claro). */
static uint8_t *section_data(const Record *r, int k, uint32_t *len) {
    uint32_t raw_len;
    const uint8_t *raw = record_section(r, k, &raw_len);
    if (!raw || !raw_len) return NULL;
    if (is_ulz(raw)) return ulz_decompress(raw, raw_len, len);
    uint8_t *c = malloc(raw_len);
    memcpy(c, raw, raw_len);
    *len = raw_len;
    return c;
}

/* ---- vertices en grupos de 3 (x0,y0,x1,y1,x2,y2,z0,z1,z2,pad) ---- */

static int read_vertices(const uint8_t *m, uint32_t len, uint32_t off, float scale, float out[][3], uint32_t max,
                         uint32_t *n_out, uint32_t *end) {
    if (off + 4 > len) return -1;
    uint32_t n = rd32(m + off), groups = (n + 2) / 3;
    if (n > max || off + 4 + 20 * groups > len) return -1;
    for (uint32_t g = 0; g < groups; g++) {
        const uint8_t *q = m + off + 4 + 20 * g;
        for (int k = 0; k < 3; k++) {
            uint32_t i = 3 * g + k;
            if (i >= n) break;
            /* PS1 (x, y abajo, z al frente) -> GL (x, -y, -z): rotacion, sin espejo */
            out[i][0] = rds16(q + 4 * k) * scale;
            out[i][1] = -rds16(q + 4 * k + 2) * scale;
            out[i][2] = -rds16(q + 12 + 2 * k) * scale;
        }
    }
    *n_out = n;
    *end = off + 4 + 20 * groups;
    return 0;
}

static void emit_tri(Mesh *mesh, float (*v)[3], const uint8_t idx[3], const uint8_t uv[3][2],
                     const uint8_t col[3][3], float (*nrm)[3], const uint8_t nidx[3], uint16_t tpage, uint16_t clut,
                     float ox, float oz) {
    for (int k = 0; k < 3; k++) {
        Vtx t;
        memset(&t, 0, sizeof t);
        t.x = v[idx[k]][0] + ox;
        t.y = v[idx[k]][1];
        t.z = v[idx[k]][2] + oz;
        t.u = uv[k][0];
        t.v = uv[k][1];
        t.r = col[k][0]; t.g = col[k][1]; t.b = col[k][2]; t.a = 255;
        if (nrm) {
            t.nx = nrm[nidx[k]][0]; t.ny = nrm[nidx[k]][1]; t.nz = nrm[nidx[k]][2];
        }
        t.tpage = tpage;
        t.clut = clut;
        mesh_push(mesh, &t);
    }
}

/* Primitiva de 4 indices -> 1 o 2 triangulos con el corte del PS1: (0,1,2) + (1,3,2). */
static void emit_prim(Mesh *mesh, float (*v)[3], const uint8_t idx[4], const uint8_t uv[4][2],
                      const uint8_t col[4][3], float (*nrm)[3], const uint8_t nidx[4], int quad, uint16_t tpage,
                      uint16_t clut, float ox, float oz) {
    uint8_t i1[3] = {idx[0], idx[1], idx[2]}, n1[3] = {nidx[0], nidx[1], nidx[2]};
    uint8_t u1[3][2] = {{uv[0][0], uv[0][1]}, {uv[1][0], uv[1][1]}, {uv[2][0], uv[2][1]}};
    uint8_t c1[3][3];
    memcpy(c1[0], col[0], 3); memcpy(c1[1], col[1], 3); memcpy(c1[2], col[2], 3);
    emit_tri(mesh, v, i1, u1, c1, nrm, n1, tpage, clut, ox, oz);
    if (quad) {
        uint8_t i2[3] = {idx[1], idx[3], idx[2]}, n2[3] = {nidx[1], nidx[3], nidx[2]};
        uint8_t u2[3][2] = {{uv[1][0], uv[1][1]}, {uv[3][0], uv[3][1]}, {uv[2][0], uv[2][1]}};
        uint8_t c2[3][3];
        memcpy(c2[0], col[1], 3); memcpy(c2[1], col[3], 3); memcpy(c2[2], col[2], 3);
        emit_tri(mesh, v, i2, u2, c2, nrm, n2, tpage, clut, ox, oz);
    }
}

/* ---- Model (teselas: off_vertices = 0x18, relleno de 12 B por primitiva) ---- */

static int add_tile_model(Scene *s, const uint8_t *m, uint32_t len, uint32_t tile) {
    static float v[256][3];
    uint32_t n, vend;
    if (len < 24 || rd16(m) != 0) return -1;
    uint32_t ov = rd32(m + 4), op = rd32(m + 12), oe = rd32(m + 16);
    if (ov != 0x18 || read_vertices(m, len, ov, 1.0f, v, 256, &n, &vend) || vend != op) return -1;
    float top = -1e9f;
    for (uint32_t i = 0; i < n; i++) if (v[i][1] > top) top = v[i][1];
    s->tile_top[tile] = n ? top : 0;
    s->tile_first[tile] = s->tiles.n;
    struct { uint32_t off, size, colors; } blocks[2] = {{op, 36, 1}, {oe, 48, 4}};
    for (int bk = 0; bk < 2; bk++) {
        uint32_t off = blocks[bk].off;
        if (off + 4 > len) return -1;
        uint32_t np = rd32(m + off);
        if (off + 4 + blocks[bk].size * np > len) return -1;
        for (uint32_t j = 0; j < np; j++) {
            const uint8_t *q = m + off + 4 + blocks[bk].size * j;
            uint8_t idx[4] = {q[16], q[17], q[18], q[19]}, nid[4] = {0};
            if (idx[0] >= n || idx[1] >= n || idx[2] >= n || idx[3] >= n) return -1;
            uint8_t uv[4][2] = {{q[20], q[21]}, {q[24], q[25]}, {q[28], q[29]}, {q[30], q[31]}};
            uint8_t col[4][3];
            for (int k = 0; k < 4; k++) {
                const uint8_t *c = q + 32 + (blocks[bk].colors == 4 ? 4 * k : 0);
                col[k][0] = c[0]; col[k][1] = c[1]; col[k][2] = c[2];
            }
            emit_prim(&s->tiles, v, idx, uv, col, NULL, nid, idx[3] != idx[2], rd16(q + 26), rd16(q + 22), 0, 0);
        }
    }
    s->tile_count[tile] = s->tiles.n - s->tile_first[tile];
    return 0;
}

int scene_load_mission(Scene *s, Disc *d, AcePack *p, uint32_t record) {
    uint32_t size, len;
    uint8_t *raw = pack_read_record(d, p, record, &size);
    Record r;
    if (!raw || record_parse(&r, raw, size) || r.type != 17 || r.nsec < 16) { free(raw); return -1; }
    /* texturas: secciones 10-14 */
    for (int k = 10; k <= 14; k++) {
        uint8_t *t = section_data(&r, k, &len);
        if (t) { tims_upload(s, t, len); free(t); }
    }
    /* mapa de teselas: seccion 0 (256x256 u16) */
    uint8_t *tm = section_data(&r, 0, &len);
    if (!tm || len < 131072) { free(tm); free(raw); return -2; }
    for (int i = 0; i < 65536; i++) s->tilemap[i] = rd16(tm + 2 * i);
    free(tm);
    /* teselas: seccion 1, tabla de modelos; indice del mapa en base 1 */
    uint8_t *tl = section_data(&r, 1, &len);
    static uint32_t st[MAX_TILES], en[MAX_TILES];
    uint32_t n = tl ? offset_table(tl, len, st, en, MAX_TILES - 1) : 0;
    if (!n) { free(tl); free(raw); return -3; }
    s->tiles.n = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (add_tile_model(s, tl + st[i], en[i] - st[i], i + 1)) { free(tl); free(raw); return -4; }
    }
    s->ntiles = n;
    free(tl);
    free(raw);
    return 0;
}

/* ---- avion del jugador: preludio del tipo 4, tabla de 12 LodModel ---- */

static int add_plane_model(Scene *s, const uint8_t *m, uint32_t len) {
    static float v[512][3], v2[512][3], nrm[1024][3];
    uint32_t n, n2, end, end2;
    if (len < 32 || rd16(m) != 0 || rd32(m + 4) != 0x1C) return -1;
    uint32_t ox2 = rd32(m + 8), ox4 = rd32(m + 12), onr = rd32(m + 16), og3 = rd32(m + 20), og4 = rd32(m + 24);
    if (onr == 0xFFFFFFFFu) return 0;                       /* solo vertices: estado alternativo */
    if (read_vertices(m, len, 0x1C, 0.25f, v, 512, &n, &end)) return -1;
    if (ox2 != 0x1C) {                                      /* usar el bloque x4 (maxima precision) */
        if (read_vertices(m, len, ox4, 0.25f, v2, 512, &n2, &end2) || n2 != n) return -1;
        memcpy(v, v2, sizeof(float) * 3 * n);
    } else {
        for (uint32_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) v[i][k] *= 4.0f;
    }
    int lit = onr != 0;
    uint32_t nn = 0;
    if (lit) {
        nn = (og3 - onr) / 8;
        if (nn > 1024 || og3 > len) return -1;
        for (uint32_t i = 0; i < nn; i++) {
            const uint8_t *q = m + onr + 8 * i;
            nrm[i][0] = rds16(q) / 4096.0f;
            nrm[i][1] = -rds16(q + 2) / 4096.0f;
            nrm[i][2] = -rds16(q + 4) / 4096.0f;
        }
    }
    uint32_t sz3 = lit ? 24 : 32, sz4 = lit ? 24 : 36;
    struct { uint32_t off, size; int quad; } blocks[2] = {{og3, sz3, 0}, {og4, sz4, 1}};
    for (int bk = 0; bk < 2; bk++) {
        uint32_t off = blocks[bk].off;
        if (off + 4 > len) return -1;
        uint32_t np = rd32(m + off);
        if (off + 4 + blocks[bk].size * np > len) return -1;
        for (uint32_t j = 0; j < np; j++) {
            const uint8_t *q = m + off + 4 + blocks[bk].size * j;
            int quad = blocks[bk].quad;
            uint8_t idx[4], nid[4] = {0, 0, 0, 0}, uv[4][2], col[4][3];
            uint16_t tpage, clut;
            if (lit) {
                for (int k = 0; k < 4; k++) { idx[k] = q[4 + k]; nid[k] = q[8 + k]; }
                uv[0][0] = q[12]; uv[0][1] = q[13]; clut = rd16(q + 14);
                uv[1][0] = q[16]; uv[1][1] = q[17]; tpage = rd16(q + 18);
                uv[2][0] = q[20]; uv[2][1] = q[21]; uv[3][0] = q[22]; uv[3][1] = q[23];
                memset(col, 128, sizeof col);
            } else if (!quad) {
                idx[0] = q[4]; idx[1] = q[5]; idx[2] = q[6]; idx[3] = q[6];
                for (int k = 0; k < 3; k++) memcpy(col[k], q + 8 + 4 * k, 3);
                memcpy(col[3], col[2], 3);
                uv[0][0] = q[20]; uv[0][1] = q[21]; clut = rd16(q + 22);
                uv[1][0] = q[24]; uv[1][1] = q[25]; tpage = rd16(q + 26);
                uv[2][0] = q[28]; uv[2][1] = q[29]; uv[3][0] = q[28]; uv[3][1] = q[29];
            } else {
                for (int k = 0; k < 4; k++) { idx[k] = q[4 + k]; memcpy(col[k], q + 8 + 4 * k, 3); }
                uv[0][0] = q[24]; uv[0][1] = q[25]; clut = rd16(q + 26);
                uv[1][0] = q[28]; uv[1][1] = q[29]; tpage = rd16(q + 30);
                uv[2][0] = q[32]; uv[2][1] = q[33]; uv[3][0] = q[34]; uv[3][1] = q[35];
            }
            if (!quad) { idx[3] = idx[2]; nid[3] = nid[2]; }
            for (int k = 0; k < 4; k++) {
                if (idx[k] >= n || (lit && nid[k] >= nn)) return -1;
            }
            emit_prim(&s->plane, v, idx, uv, col, lit ? nrm : NULL, nid, quad && idx[3] != idx[2], tpage, clut, 0, 0);
        }
    }
    /* los modelos clasicos no traen normales: se dejan a 0 y el shader no los ilumina */
    return 0;
}

int scene_load_aircraft(Scene *s, Disc *d, AcePack *p, uint32_t record) {
    uint32_t size, len;
    uint8_t *raw = pack_read_record(d, p, record, &size);
    Record r;
    if (!raw || record_parse(&r, raw, size) || r.type != 4) { free(raw); return -1; }
    /* piel e icono: seccion 1 */
    uint8_t *t = section_data(&r, 1, &len);
    if (t) { tims_upload(s, t, len); free(t); }
    /* modelos: preludio */
    uint8_t *pm = section_data(&r, -1, &len);
    static uint32_t st[32], en[32];
    uint32_t n = pm ? offset_table(pm, len, st, en, 32) : 0;
    if (!n) { free(pm); free(raw); return -2; }
    s->plane.n = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (add_plane_model(s, pm + st[i], en[i] - st[i])) { free(pm); free(raw); return -3; }
    }
    free(pm);
    free(raw);
    return 0;
}

float scene_ground(const Scene *s, float x, float z) {
    /* PS1: celda = floor(coord / 2048) + 128 ; GL z = -z PS1 */
    int cx = (int)floorf(x / 2048.0f) + 128, cz = (int)floorf(-z / 2048.0f) + 128;
    if (cx < 0 || cx > 255 || cz < 0 || cz > 255) return -2000.0f;
    uint32_t t = s->tilemap[cz * 256 + cx] & 0x1FF;
    return (t && t <= s->ntiles) ? s->tile_top[t] : -2000.0f;
}
