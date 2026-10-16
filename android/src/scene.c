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

static void part_free(Part *p) {
    for (int i = 0; i < 5; i++) free(p->state[i].v);
    memset(p, 0, sizeof *p);
}

void scene_clear_meshes(Scene *s) {
    free(s->tiles.v);
    free(s->plane.v);
    part_free(&s->wing); part_free(&s->dorsal); part_free(&s->rudder);
    for (uint32_t i = 0; i < MAX_MODELS; i++) free(s->models[i].v);
    free(s->missile.v);
    memset(&s->tiles, 0, sizeof s->tiles);
    memset(&s->plane, 0, sizeof s->plane);
    memset(s->models, 0, sizeof s->models);
    memset(&s->missile, 0, sizeof s->missile);
    s->nmodels = 0;
}

void scene_free(Scene *s) {
    scene_clear_meshes(s);
    sound_free_slots(s->sounds, 0, SOUND_SLOTS - 1);
    memset(s, 0, sizeof *s);
}

static void mesh_bounds(Mesh *m) {
    float r = 0;
    for (int k = 0; k < 3; k++) { m->bmin[k] = 1e9f; m->bmax[k] = -1e9f; }
    for (uint32_t i = 0; i < m->n; i++) {
        float p[3] = {m->v[i].x, m->v[i].y, m->v[i].z};
        for (int k = 0; k < 3; k++) {
            if (p[k] < m->bmin[k]) m->bmin[k] = p[k];
            if (p[k] > m->bmax[k]) m->bmax[k] = p[k];
        }
        float a = fabsf(m->v[i].x), b = fabsf(m->v[i].y), c = fabsf(m->v[i].z);
        if (a > r) r = a;
        if (b > r) r = b;
        if (c > r) r = c;
    }
    m->radius = r;
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


/* ---- Model (teselas: off_vertices = 0x18; objetos de s4: 0x24) ----
 * Bloque 1: POLY_FT4 de 36 B (un color); bloque 2: POLY_GT4 de 48 B (4 colores).
 * Las teselas llevan 12 B de relleno por primitiva al final de cada bloque. */

static int add_model(Mesh *dst, const uint8_t *m, uint32_t len, float *top_out) {
    static float v[256][3];
    uint32_t n, vend;
    if (len < 24 || rd16(m) != 0) return -1;
    uint32_t ov = rd32(m + 4), op = rd32(m + 12), oe = rd32(m + 16);
    if ((ov != 0x18 && ov != 0x24) || read_vertices(m, len, ov, 1.0f, v, 256, &n, &vend) || vend != op) return -1;
    if (top_out) {
        float top = -1e9f;
        for (uint32_t i = 0; i < n; i++) if (v[i][1] > top) top = v[i][1];
        *top_out = n ? top : 0;
    }
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
            emit_prim(dst, v, idx, uv, col, NULL, nid, idx[3] != idx[2], rd16(q + 26), rd16(q + 22), 0, 0);
        }
    }
    return 0;
}

static int add_tile_model(Scene *s, const uint8_t *m, uint32_t len, uint32_t tile) {
    s->tile_first[tile] = s->tiles.n;
    if (add_model(&s->tiles, m, len, &s->tile_top[tile])) return -1;
    s->tile_count[tile] = s->tiles.n - s->tile_first[tile];
    return 0;
}

/* ---- cielo: preludio del tipo 17 (comprimido con ULZ, 312 B) ---- */

static void rgb3(float o[3], const uint8_t *p, int or3) {
    for (int k = 0; k < 3; k++) o[k] = (or3 ? (p[k] | 3) : p[k]) / 255.0f;
}

static void parse_sky(Sky *k, const uint8_t *p, uint32_t len) {
    memset(k, 0, sizeof *k);
    if (len != 312) return;
    rgb3(k->below, p + 4, 0);
    rgb3(k->fog, p + 8, 0);
    rgb3(k->high_sun, p + 0x14, 0);
    rgb3(k->high_away, p + 0x18, 0);
    rgb3(k->zenith, p + 0x1C, 1);
    rgb3(k->low_sun, p + 0x24, 0);
    rgb3(k->low_away, p + 0x28, 0);
    int h10 = rds16(p + 0x10);
    k->band_alt[0] = -2.0f * rds16(p + 0x22);
    k->band_alt[1] = -2.0f * rds16(p + 0x20);
    k->band_alt[2] = -2.0f * h10;
    k->band_alt[3] = -h10 / 2.0f;
    k->sun_heading = rds16(p + 0x3E) * (6.2831853f / 4096.0f);
    k->sun_elev = rds16(p + 0x3C) * (6.2831853f / 4096.0f);
    /* PS1: (cos rx sin ry, -sin rx, cos rx cos ry) con y abajo -> GL (x, -y, -z) */
    k->sun_dir[0] = cosf(k->sun_elev) * sinf(k->sun_heading);
    k->sun_dir[1] = sinf(k->sun_elev);
    k->sun_dir[2] = -cosf(k->sun_elev) * cosf(k->sun_heading);
    k->sun = rds16(p + 0x6C);   /* byte bajo: disco; byte alto: destello (FUN_8008A310) */
    k->haze = p[0x6E];
    k->weather = p[0x6F];
    k->ok = 1;
}

/* ---- guion (s6): tablas de aparicion ---- */

static void parse_spawn(Spawn *o, const uint8_t *r) {
    o->team = r[0];
    o->type = r[1];
    o->model = r[2] == 0xFF ? -1 : r[2];
    /* posicion << 12 en PS1 -> GL (x, -y, -z) */
    o->x = (int32_t)rd32(r + 4) / 4096.0f;
    o->y = -(int32_t)rd32(r + 8) / 4096.0f;
    o->z = -(int32_t)rd32(r + 12) / 4096.0f;
    o->yaw = (rd16(r + 0x16) & 0x3FFF) * (6.2831853f / 16384.0f);  /* 0x4000 = 360 (FUN_80030534) */
    o->mesh = o->mesh_dead = -1;
}

static void parse_script(Scene *s, const uint8_t *d, uint32_t len) {
    static uint32_t st[16], en[16];
    uint32_t n = offset_table(d, len, st, en, 16);
    if (n == 3 && en[0] - st[0] > 8) {        /* tres variantes: se usa la primera */
        const uint8_t *v = d + st[0];
        uint32_t vl = en[0] - st[0];
        uint32_t n2 = offset_table(v, vl, st, en, 16);
        if (!n2) return;
        d = v; len = vl;
    } else if (!n) {
        return;
    }
    const uint8_t *e = d + st[0];
    uint32_t el = en[0] - st[0];
    if (el < 0x48) return;
    uint32_t na = e[8], no = e[0xC];
    uint32_t oa = rd32(e + 0x10 + 4 * 2) * 4, oo = rd32(e + 0x10 + 4 * 8) * 4;
    for (uint32_t i = 0; i < na && s->nair < MAX_SPAWNS && oa + 44 * (i + 1) <= el; i++)
        parse_spawn(&s->air[s->nair++], e + oa + 44 * i);
    for (uint32_t i = 0; i < no && s->nobj < MAX_SPAWNS && oo + 36 * (i + 1) <= el; i++)
        parse_spawn(&s->obj[s->nobj++], e + oo + 36 * i);
}

static int add_plane_model(Mesh *dst, const uint8_t *m, uint32_t len, const uint8_t *alt, uint32_t altlen);

/* Busca o carga el modelo `key` (id LOD, o indice de s4 | 0x100). */
static int scene_model(Scene *s, int key, const uint8_t *lod, uint32_t lodlen, const uint8_t *s3, uint32_t s3len,
                       const uint8_t *objs, uint32_t objlen) {
    for (uint32_t i = 0; i < s->nmodels; i++) if (s->model_key[i] == key) return (int)i;
    if (s->nmodels >= MAX_MODELS) return -1;
    static uint32_t st[256], en[256];
    Mesh *dst = &s->models[s->nmodels];
    dst->n = 0;
    if (key & 0x100) {
        uint32_t n = objs ? offset_table(objs, objlen, st, en, 256) : 0;
        int i = key & 0xFF;
        if ((uint32_t)i >= n || add_model(dst, objs + st[i], en[i] - st[i], NULL)) return -1;
    } else {
        /* grupo LOD: ranuras id*4 .. id*4+2 de la tabla unica (s2, y s3 desde la 96) */
        const uint8_t *tb = lod;
        uint32_t tl = lodlen, base = 0;
        if (key * 4 >= 96) { tb = s3; tl = s3len; base = 96; }
        uint32_t n = tb ? offset_table(tb, tl, st, en, 256) : 0;
        int ok = 0;
        for (int lv = 0; lv < 3 && !ok; lv++) {
            uint32_t i = key * 4 + lv - base;
            if (i >= n || en[i] - st[i] < 32) continue;
            dst->n = 0;
            ok = add_plane_model(dst, tb + st[i], en[i] - st[i], NULL, 0) == 0 && dst->n;
        }
        if (!ok) return -1;
    }
    mesh_bounds(dst);
    s->model_key[s->nmodels] = key;
    return (int)s->nmodels++;
}

int scene_load_common(Scene *s, Disc *d, AcePack *p) {
    DiscFile f;
    s->pal = disc_find_prefix(d, "SCES_", &f) == 0 || disc_find_prefix(d, "SLES_", &f) == 0;
    uint32_t size, len;
    uint8_t *raw = pack_read_record(d, p, 0, &size);
    Record r;
    if (!raw || record_parse(&r, raw, size) || r.nsec < 2) { free(raw); return -1; }
    int n = 0;
    for (int k = 0; k < 2; k++) {
        uint8_t *t = section_data(&r, k, &len);
        if (t) { n += tims_upload(s, t, len); free(t); }
    }
    /* banco comun de efectos: s3, ranuras 0-75 */
    uint8_t *b = r.nsec > 3 ? section_data(&r, 3, &len) : NULL;
    if (b) { sound_load_bank(s->sounds, b, len); free(b); }
    free(raw);
    return n == 2 ? 0 : -2;
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
    /* las misiones 23 y 43 no tienen terreno (mapa vacio, s1 de 8 B): es valido, no un error */
    s->tiles.n = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (add_tile_model(s, tl + st[i], en[i] - st[i], i + 1)) { free(tl); free(raw); return -4; }
    }
    s->ntiles = n;
    free(tl);
    /* cielo y niebla: preludio */
    uint8_t *pre = section_data(&r, -1, &len);
    if (pre) { parse_sky(&s->sky, pre, len); free(pre); }
    /* guion: tablas de aparicion */
    s->nair = s->nobj = 0;
    uint8_t *sc = section_data(&r, 6, &len);
    if (sc) { parse_script(s, sc, len); free(sc); }
    /* modelos: LOD de s2/s3 para aviones, s4 para objetos */
    uint32_t l2 = 0, l3 = 0, l4 = 0;
    uint8_t *la = section_data(&r, 2, &l2), *wp = section_data(&r, 3, &l3), *ob = section_data(&r, 4, &l4);
    s->nmodels = 0;
    for (int i = 0; i < s->nair; i++)
        if (s->air[i].model >= 0 && i > 0)
            s->air[i].mesh = scene_model(s, s->air[i].model, la, l2, wp, l3, ob, l4);
    for (int i = 0; i < s->nobj; i++)
        if (s->obj[i].model >= 0)
            s->obj[i].mesh = scene_model(s, s->obj[i].model | 0x100, la, l2, wp, l3, ob, l4);
    /* version destruida (SIN RESOLVER): el modelo anterior, si es mas bajo que el intacto */
    for (int i = 0; i < s->nobj; i++) {
        Spawn *o = &s->obj[i];
        if (o->mesh < 0 || o->model < 1) continue;
        int dm = scene_model(s, (o->model - 1) | 0x100, la, l2, wp, l3, ob, l4);
        if (dm < 0) continue;
        float top_i = -1e9f, top_d = -1e9f;
        for (uint32_t k = 0; k < s->models[o->mesh].n; k++) if (s->models[o->mesh].v[k].y > top_i) top_i = s->models[o->mesh].v[k].y;
        for (uint32_t k = 0; k < s->models[dm].n; k++) if (s->models[dm].v[k].y > top_d) top_d = s->models[dm].v[k].y;
        if (top_d < top_i - 4) o->mesh_dead = dm;
    }
    /* misil: grupo 0x18 (arma 1 = id + 0x17), o s3 entrada 3 si no hay */
    static uint32_t ls[128], le[128];
    s->missile.n = 0;
    uint32_t nw = wp ? offset_table(wp, l3, ls, le, 128) : 0;
    if (nw > 0 && le[0] - ls[0] >= 32) add_plane_model(&s->missile, wp + ls[0], le[0] - ls[0], NULL, 0);
    if (!s->missile.n && nw > 3) add_plane_model(&s->missile, wp + ls[3], le[3] - ls[3], NULL, 0);
    mesh_bounds(&s->missile);
    free(la); free(wp); free(ob);
    /* banco de sonido de la mision: s15, ranuras 88-107 */
    sound_free_slots(s->sounds, 88, SOUND_SLOTS - 1);
    uint8_t *sb = section_data(&r, 15, &len);
    if (sb) { sound_load_bank(s->sounds, sb, len); free(sb); }
    free(raw);
    return 0;
}

/* ---- avion del jugador: preludio del tipo 4, tabla de 12 LodModel ---- */

/* Vertices de un LodModel a escala real: el bloque x4 si existe; si no, el base x4. */
static int lod_vertices(const uint8_t *m, uint32_t len, float (*v)[3], uint32_t *n) {
    static float v2[512][3];
    uint32_t end, n2, end2;
    if (len < 32 || rd16(m) != 0 || rd32(m + 4) != 0x1C) return -1;
    uint32_t ox2 = rd32(m + 8), ox4 = rd32(m + 12);
    if (read_vertices(m, len, 0x1C, 0.25f, v, 512, n, &end)) return -1;
    if (ox2 != 0x1C) {
        if (read_vertices(m, len, ox4, 0.25f, v2, 512, &n2, &end2) || n2 != *n) return -1;
        memcpy(v, v2, sizeof(float) * 3 * *n);
    } else {
        for (uint32_t i = 0; i < *n; i++) for (int k = 0; k < 3; k++) v[i][k] *= 4.0f;
    }
    return 0;
}

/* Emite las primitivas de `m`; si `alt` no es NULL, con los vertices de ese modelo
 * (estado alternativo de una pieza movil: mismo numero de vertices). */
static int add_plane_model(Mesh *dst, const uint8_t *m, uint32_t len, const uint8_t *alt, uint32_t altlen) {
    static float v[512][3], nrm[1024][3];
    uint32_t n, na;
    if (len < 32 || rd16(m) != 0 || rd32(m + 4) != 0x1C) return -1;
    uint32_t onr = rd32(m + 16), og3 = rd32(m + 20), og4 = rd32(m + 24);
    if (onr == 0xFFFFFFFFu) return 0;                       /* solo vertices: estado alternativo */
    if (lod_vertices(m, len, v, &n)) return -1;
    if (alt) {
        static float va[512][3];
        if (lod_vertices(alt, altlen, va, &na) || na != n) return -1;
        memcpy(v, va, sizeof(float) * 3 * n);
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
            emit_prim(dst, v, idx, uv, col, lit ? nrm : NULL, nid, quad && idx[3] != idx[2], tpage, clut, 0, 0);
        }
    }
    /* los modelos clasicos no traen normales: se dejan a 0 y el shader no los ilumina */
    return 0;
}

/* Desplazamiento medio en y (GL) de las puntas de ala de `alt` respecto a `base`, por lado. */
static void wing_delta(const uint8_t *base, uint32_t bl, const uint8_t *alt, uint32_t al, float *left, float *right,
                       float *span) {
    static float vb[512][3], va[512][3];
    uint32_t nb, na;
    *left = *right = 0;
    if (lod_vertices(base, bl, vb, &nb) || lod_vertices(alt, al, va, &na) || na != nb) return;
    float lim = 0;
    for (uint32_t i = 0; i < nb; i++) if (fabsf(vb[i][0]) > lim) lim = fabsf(vb[i][0]);
    if (span) *span = lim;
    lim *= 0.4f;
    for (uint32_t i = 0; i < nb; i++) {
        float dy = va[i][1] - vb[i][1];
        if (vb[i][0] < -lim) *left += dy;
        else if (vb[i][0] > lim) *right += dy;
    }
}

static float rudder_delta(const uint8_t *base, uint32_t bl, const uint8_t *alt, uint32_t al) {
    static float vb[512][3], va[512][3];
    uint32_t nb, na;
    float dx = 0;
    if (lod_vertices(base, bl, vb, &nb) || lod_vertices(alt, al, va, &na) || na != nb) return 0;
    for (uint32_t i = 0; i < nb; i++) dx += va[i][0] - vb[i][0];
    return dx;
}

static int build_part(Part *pt, const uint8_t *pm, const uint32_t *st, const uint32_t *en, int first, int count) {
    pt->nstates = 0;
    for (int k = 0; k < count; k++) {
        Mesh *ms = &pt->state[k];
        ms->n = 0;
        const uint8_t *alt = k ? pm + st[first + k] : NULL;
        uint32_t altlen = k ? en[first + k] - st[first + k] : 0;
        if (add_plane_model(ms, pm + st[first], en[first] - st[first], alt, altlen) || !ms->n) return -1;
        pt->nstates++;
    }
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
    /* banco de sonido del avion (motor): s2, ranuras 84-85 */
    sound_free_slots(s->sounds, 84, 87);
    uint8_t *sb = section_data(&r, 2, &len);
    if (sb) { sound_load_bank(s->sounds, sb, len); free(sb); }
    /* modelos: preludio */
    uint8_t *pm = section_data(&r, -1, &len);
    static uint32_t st[32], en[32];
    uint32_t n = pm ? offset_table(pm, len, st, en, 32) : 0;
    if (!n) { free(pm); free(raw); return -2; }
    free(s->plane.v);
    part_free(&s->wing); part_free(&s->dorsal); part_free(&s->rudder);
    memset(&s->plane, 0, sizeof s->plane);
    /* patron de 12: 0 tobera, 1 fuselaje, 2-6 alas, 7-8 dorsal, 9-11 deriva (tools/acesurf.py) */
    int parts = n == 12 &&
                !build_part(&s->wing, pm, st, en, 2, 5) &&
                !build_part(&s->dorsal, pm, st, en, 7, 2) &&
                !build_part(&s->rudder, pm, st, en, 9, 3);
    if (parts) {
        for (int i = 0; i < 2; i++)
            if (add_plane_model(&s->plane, pm + st[i], en[i] - st[i], NULL, 0)) parts = 0;
    }
    if (parts) {
        /* que estado de ala es cada maniobra: medido sobre los vertices (y GL arriba) */
        float l, rr, span = 0;
        s->wing_roll_right = s->wing_roll_left = s->wing_nose_up = s->wing_nose_down = 0;
        for (int k = 1; k < 5; k++) {
            wing_delta(pm + st[2], en[2] - st[2], pm + st[2 + k], en[2 + k] - st[2 + k], &l, &rr, &span);
            if (fabsf(l) + fabsf(rr) < 1e-3f) continue;
            if ((l > 0) != (rr > 0)) {
                /* aleron izquierdo abajo (l < 0 en GL) -> alabeo a la derecha */
                if (l < 0) s->wing_roll_right = k; else s->wing_roll_left = k;
            } else {
                /* superficies arriba (dy > 0 en GL) -> morro arriba */
                if (l > 0) s->wing_nose_up = k; else s->wing_nose_down = k;
            }
        }
        s->wingspan = span;
        float d1 = rudder_delta(pm + st[9], en[9] - st[9], pm + st[10], en[10] - st[10]);
        s->rudder_right = d1 > 0 ? 1 : 2;
        s->rudder_left = d1 > 0 ? 2 : 1;
    } else {
        part_free(&s->wing); part_free(&s->dorsal); part_free(&s->rudder);
        free(s->plane.v);
        memset(&s->plane, 0, sizeof s->plane);
        for (uint32_t i = 0; i < n; i++) {
            if (add_plane_model(&s->plane, pm + st[i], en[i] - st[i], NULL, 0)) { free(pm); free(raw); return -3; }
        }
    }
    mesh_bounds(&s->plane);
    if (!parts) s->wingspan = s->plane.radius * 0.8f;
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

/* ---- colisiones contra los triangulos de las teselas ----
 * Cada tesela esta en coordenadas locales al centro de su celda (como en el dibujo). */

static void vsub3(float o[3], const float a[3], const float b[3]) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static float vdot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* Punto del triangulo abc mas cercano a p (Ericson, "Real-Time Collision Detection" 5.1.5). */
static void closest_on_tri(float o[3], const float p[3], const float a[3], const float b[3], const float c[3]) {
    float ab[3], ac[3], ap[3], bp[3], cp[3];
    vsub3(ab, b, a); vsub3(ac, c, a); vsub3(ap, p, a);
    float d1 = vdot3(ab, ap), d2 = vdot3(ac, ap);
    if (d1 <= 0 && d2 <= 0) { memcpy(o, a, 12); return; }
    vsub3(bp, p, b);
    float d3 = vdot3(ab, bp), d4 = vdot3(ac, bp);
    if (d3 >= 0 && d4 <= d3) { memcpy(o, b, 12); return; }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        float v = d1 / (d1 - d3);
        for (int k = 0; k < 3; k++) o[k] = a[k] + v * ab[k];
        return;
    }
    vsub3(cp, p, c);
    float d5 = vdot3(ab, cp), d6 = vdot3(ac, cp);
    if (d6 >= 0 && d5 <= d6) { memcpy(o, c, 12); return; }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        float w = d2 / (d2 - d6);
        for (int k = 0; k < 3; k++) o[k] = a[k] + w * ac[k];
        return;
    }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (int k = 0; k < 3; k++) o[k] = b[k] + w * (c[k] - b[k]);
        return;
    }
    float den = 1.0f / (va + vb + vc), v = vb * den, w = vc * den;
    for (int k = 0; k < 3; k++) o[k] = a[k] + ab[k] * v + ac[k] * w;
}

/* Recorre los triangulos de las celdas que toca la caja [x0,x1]x[z0,z1] (GL). */
typedef int (*tri_fn)(const float a[3], const float b[3], const float c[3], void *ctx);

static int for_tris(const Scene *s, float x0, float x1, float z0, float z1, tri_fn fn, void *ctx) {
    /* PS1: celda = floor(coord / 2048) + 128 ; GL z = -z PS1 */
    int cx0 = (int)floorf(x0 / 2048.0f) + 128, cx1 = (int)floorf(x1 / 2048.0f) + 128;
    int cz0 = (int)floorf(-z1 / 2048.0f) + 128, cz1 = (int)floorf(-z0 / 2048.0f) + 128;
    for (int cz = cz0; cz <= cz1; cz++) {
        if (cz < 0 || cz > 255) continue;
        for (int cx = cx0; cx <= cx1; cx++) {
            if (cx < 0 || cx > 255) continue;
            uint32_t t = s->tilemap[cz * 256 + cx] & 0x1FF;
            if (!t || t > s->ntiles) continue;
            float ox = (cx - 128) * 2048.0f + 1024, oz = -((cz - 128) * 2048.0f + 1024);
            const Vtx *v = s->tiles.v + s->tile_first[t];
            for (uint32_t i = 0; i + 2 < s->tile_count[t]; i += 3) {
                float a[3] = {v[i].x + ox, v[i].y, v[i].z + oz}, b[3] = {v[i + 1].x + ox, v[i + 1].y, v[i + 1].z + oz},
                      c[3] = {v[i + 2].x + ox, v[i + 2].y, v[i + 2].z + oz};
                if (fn(a, b, c, ctx)) return 1;
            }
        }
    }
    return 0;
}

typedef struct { float p[3], r2; } SphereCtx;

static int tri_sphere(const float a[3], const float b[3], const float c[3], void *ctx) {
    SphereCtx *q = ctx;
    float o[3], d[3];
    closest_on_tri(o, q->p, a, b, c);
    vsub3(d, o, q->p);
    return vdot3(d, d) <= q->r2;
}

int scene_hit_sphere(const Scene *s, float x, float y, float z, float r) {
    SphereCtx q = {{x, y, z}, r * r};
    return for_tris(s, x - r, x + r, z - r, z + r, tri_sphere, &q);
}

typedef struct { float p0[3], d[3]; } SegCtx;

/* Moller-Trumbore: el segmento p0 + t*d (0 <= t <= 1) corta el triangulo? */
static int tri_segment(const float a[3], const float b[3], const float c[3], void *ctx) {
    SegCtx *q = ctx;
    float e1[3], e2[3], pv[3], tv[3], qv[3];
    vsub3(e1, b, a); vsub3(e2, c, a);
    pv[0] = q->d[1] * e2[2] - q->d[2] * e2[1]; pv[1] = q->d[2] * e2[0] - q->d[0] * e2[2]; pv[2] = q->d[0] * e2[1] - q->d[1] * e2[0];
    float det = vdot3(e1, pv);
    if (fabsf(det) < 1e-9f) return 0;
    float inv = 1.0f / det;
    vsub3(tv, q->p0, a);
    float u = vdot3(tv, pv) * inv;
    if (u < 0 || u > 1) return 0;
    qv[0] = tv[1] * e1[2] - tv[2] * e1[1]; qv[1] = tv[2] * e1[0] - tv[0] * e1[2]; qv[2] = tv[0] * e1[1] - tv[1] * e1[0];
    float v = vdot3(q->d, qv) * inv;
    if (v < 0 || u + v > 1) return 0;
    float t = vdot3(e2, qv) * inv;
    return t >= 0 && t <= 1;
}

int scene_hit_segment(const Scene *s, const float p0[3], const float p1[3]) {
    SegCtx q = {{p0[0], p0[1], p0[2]}, {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}};
    return for_tris(s, fminf(p0[0], p1[0]), fmaxf(p0[0], p1[0]), fminf(p0[2], p1[2]), fmaxf(p0[2], p1[2]), tri_segment, &q);
}
