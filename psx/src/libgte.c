/* libgte: funciones que usan la GTE. Reimplementadas a partir de la descompilacion de
 * ACE.BIN EU (direcciones en tools/psyqcheck_gte.py) y validadas contra el codigo original
 * ejecutado en tools/mipsemu.py sobre esta misma GTE: mismos resultados en memoria y mismo
 * estado final de los 64 registros. Se replican las cargas "en crudo" de palabras de 32 bits
 * (con los bytes de relleno de las estructuras) porque cambian el estado de la GTE. */
#include <math.h>
#include <string.h>

#include "psx_gte.h"
#include "psx_libgte.h"

static PsxGte g_default;
static PsxGte *G = &g_default;

void psx_gte_set_context(void *gte) { G = gte ? (PsxGte *)gte : &g_default; }
void *psx_gte_get_context(void) { return G; }

/* palabra de 32 bits en `p` (como lw de la PS1, little-endian) */
static uint32_t w32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void put32(void *p, uint32_t v) { memcpy(p, &v, 4); }

#define CTC(r, v) psx_gte_write_ctrl(G, (r), (uint32_t)(v))
#define MTC(r, v) psx_gte_write_data(G, (r), (uint32_t)(v))
#define CFC(r) psx_gte_read_ctrl(G, (r))
#define MFC(r) psx_gte_read_data(G, (r))
#define CMD(c) psx_gte_command(G, (c))

/* ordenes tal como las codifican las macros de PsyQ */
#define C_RTPS 0x0180001u
#define C_RTPT 0x0280030u
#define C_RTV0 0x0486012u   /* MVMVA sf=1, RT, V0, sin traslacion */
#define C_RTV0TR 0x0480012u /* MVMVA sf=1, RT, V0, TR */
#define C_RTIR 0x049E012u   /* MVMVA sf=1, RT, IR */
#define C_RTIR_SF0 0x041E012u
#define C_OP0 0x170000Cu    /* OP sin desplazamiento */

static void load_matrix(int base, const MATRIX *m) {   /* RT (0), L (8), LR (16) */
    const uint8_t *p = (const uint8_t *)m;
    for (int i = 0; i < 5; i++) CTC(base + i, w32(p + 4 * i));
}

void InitGeom(void) {
    CTC(29, 0x155);
    CTC(30, 0x100);
    CTC(26, 1000);
    CTC(27, 0xFFFFEF9Eu);
    CTC(28, 0x1400000);
    CTC(24, 0);
    CTC(25, 0);
}

void ReadGeomOffset(int32_t *ofx, int32_t *ofy) {
    *ofx = (int32_t)CFC(24) >> 16;
    *ofy = (int32_t)CFC(25) >> 16;
}

/* sin firma en el ACE.BIN (0x800A55E8, 0x800A5608, 0x800A55B8, 0x800A58E8 en EU) */
void SetGeomOffset(int32_t ofx, int32_t ofy) { CTC(24, (uint32_t)ofx << 16); CTC(25, (uint32_t)ofy << 16); }
void SetGeomScreen(int32_t h) { CTC(26, (uint32_t)h); }
int32_t ReadGeomScreen(void) { return (int32_t)CFC(26); }
int32_t NormalClip(int32_t sxy0, int32_t sxy1, int32_t sxy2) {
    MTC(12, (uint32_t)sxy0);
    MTC(14, (uint32_t)sxy2);
    MTC(13, (uint32_t)sxy1);
    CMD(0x1400006);
    return (int32_t)MFC(24);
}

void SetRotMatrix(MATRIX *m) { load_matrix(0, m); }
void SetLightMatrix(MATRIX *m) { load_matrix(8, m); }
void SetColorMatrix(MATRIX *m) { load_matrix(16, m); }

void SetTransMatrix(MATRIX *m) {
    CTC(5, m->t[0]);
    CTC(6, m->t[1]);
    CTC(7, m->t[2]);
}

void SetBackColor(int32_t rbk, int32_t gbk, int32_t bbk) {
    CTC(13, (uint32_t)rbk << 4);
    CTC(14, (uint32_t)gbk << 4);
    CTC(15, (uint32_t)bbk << 4);
}

static void ldv(int n, const SVECTOR *v) {
    MTC(2 * n, w32(v));
    MTC(2 * n + 1, w32((const uint8_t *)v + 4));
}

int32_t RotTransPers(SVECTOR *v, int32_t *sxy, int32_t *p, int32_t *flag) {
    ldv(0, v);
    CMD(C_RTPS);
    *sxy = (int32_t)MFC(14);
    *p = (int32_t)MFC(8);
    *flag = (int32_t)CFC(31);
    return (int32_t)MFC(19) >> 2;
}

int32_t RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, int32_t *sxy0, int32_t *sxy1, int32_t *sxy2,
                      int32_t *p, int32_t *flag) {
    ldv(0, v0);
    ldv(1, v1);
    ldv(2, v2);
    CMD(C_RTPT);
    *sxy0 = (int32_t)MFC(12);
    *sxy1 = (int32_t)MFC(13);
    *sxy2 = (int32_t)MFC(14);
    *p = (int32_t)MFC(8);
    *flag = (int32_t)CFC(31);
    return (int32_t)MFC(19) >> 2;
}

void RotTrans(SVECTOR *v, VECTOR *out, int32_t *flag) {
    ldv(0, v);
    CMD(C_RTV0TR);
    out->vx = (int32_t)MFC(25);
    out->vy = (int32_t)MFC(26);
    out->vz = (int32_t)MFC(27);
    *flag = (int32_t)CFC(31);
}

/* m0 * m1 por columnas con RTV0; devuelve el resultado en `out` (escritura como el original,
 * incluido el IR3 de 32 bits sobre m[2][2] y su relleno) */
static void mul_columns(const MATRIX *m0, const MATRIX *m1, uint32_t ir[3][3], int load_rt) {
    const uint8_t *b = (const uint8_t *)m1;
    if (load_rt) load_matrix(0, m0);
    for (int j = 0; j < 3; j++) {
        uint32_t vxy, vz;
        if (j == 0) { vxy = (uint16_t)m1->m[0][0] | (w32(b + 4) & 0xFFFF0000u); vz = w32(b + 12); }
        else if (j == 1) { vxy = (uint16_t)m1->m[0][1] | (w32(b + 8) << 16); vz = (uint32_t)(int32_t)m1->m[2][1]; }
        else { vxy = (uint16_t)m1->m[0][2] | (w32(b + 8) & 0xFFFF0000u); vz = w32(b + 16); }
        MTC(0, vxy);
        MTC(1, vz);
        CMD(C_RTV0);
        ir[j][0] = MFC(9);
        ir[j][1] = MFC(10);
        ir[j][2] = MFC(11);
    }
}

static void store_product(MATRIX *dst, uint32_t ir[3][3]) {
    uint8_t *o = (uint8_t *)dst;
    put32(o + 0, (ir[1][0] << 16) | (ir[0][0] & 0xFFFF));
    put32(o + 12, (ir[1][2] << 16) | (ir[0][2] & 0xFFFF));
    put32(o + 4, (ir[2][0] & 0xFFFF) | (ir[0][1] << 16));
    put32(o + 8, (ir[2][1] << 16) | (ir[1][1] & 0xFFFF));
    put32(o + 16, ir[2][2]);
}

MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2) {
    uint32_t ir[3][3];
    mul_columns(m0, m1, ir, 1);
    store_product(m2, ir);
    return m2;
}

MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1) {
    uint32_t ir[3][3];
    mul_columns(m0, m1, ir, 1);
    store_product(m0, ir);
    return m0;
}

MATRIX *MulMatrix2(MATRIX *m0, MATRIX *m1) {
    uint32_t ir[3][3];
    mul_columns(m0, m1, ir, 1);
    store_product(m1, ir);
    return m1;
}

MATRIX *SetMulMatrix(MATRIX *m0, MATRIX *m1) {
    uint32_t ir[3][3];
    mul_columns(m0, m1, ir, 1);
    CTC(0, (ir[1][0] << 16) | (ir[0][0] & 0xFFFF));
    CTC(1, (ir[2][0] & 0xFFFF) | (ir[0][1] << 16));
    CTC(2, (ir[2][1] << 16) | (ir[1][1] & 0xFFFF));
    CTC(3, (ir[1][2] << 16) | (ir[0][2] & 0xFFFF));
    CTC(4, ir[2][2]);
    return m0;
}

SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v, SVECTOR *out) {
    load_matrix(0, m);
    ldv(0, v);
    CMD(C_RTV0);
    out->vx = (int16_t)MFC(9);
    out->vy = (int16_t)MFC(10);
    out->vz = (int16_t)MFC(11);
    return out;
}

/* vector largo = alto * 0x8000 + bajo, cada parte por la matriz y se recompone */
static void split(int32_t v, int32_t *hi, int32_t *lo) {
    if (v < 0) { *hi = -((int32_t)(-(uint32_t)v) >> 15); *lo = -(int32_t)((-(uint32_t)v) & 0x7FFF); }
    else { *hi = v >> 15; *lo = v & 0x7FFF; }
}

static VECTOR *apply_lv(VECTOR *v, VECTOR *out) {
    int32_t h[3], l[3];
    split(v->vx, &h[0], &l[0]);
    split(v->vy, &h[1], &l[1]);
    split(v->vz, &h[2], &l[2]);
    for (int i = 0; i < 3; i++) MTC(9 + i, (uint32_t)h[i]);
    CMD(C_RTIR_SF0);
    int32_t mh[3] = {(int32_t)MFC(25), (int32_t)MFC(26), (int32_t)MFC(27)};
    for (int i = 0; i < 3; i++) MTC(9 + i, (uint32_t)l[i]);
    CMD(C_RTIR);
    out->vx = (int32_t)MFC(25) + (int32_t)((uint32_t)mh[0] << 3);
    out->vy = (int32_t)MFC(26) + (int32_t)((uint32_t)mh[1] << 3);
    out->vz = (int32_t)MFC(27) + (int32_t)((uint32_t)mh[2] << 3);
    return out;
}

VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v, VECTOR *out) {
    load_matrix(0, m);
    return apply_lv(v, out);
}

VECTOR *ApplyRotMatrixLV(VECTOR *v, VECTOR *out) { return apply_lv(v, out); }

MATRIX *TransposeMatrix(MATRIX *m0, MATRIX *m1) {
    MATRIX t = *m0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m1->m[i][j] = t.m[j][i];
    return m1;
}

MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v) {
    int32_t s[3] = {v->vx, v->vy, v->vz};
    /* m[2][2] se escribe como palabra de 32 bits con el producto entero (pisa el relleno) */
    int32_t p22 = (int32_t)((uint32_t)(int32_t)m->m[2][2] * (uint32_t)s[2]) >> 12;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            int32_t prod = (int32_t)((uint32_t)(int32_t)m->m[i][j] * (uint32_t)s[j]);
            m->m[i][j] = (int16_t)(prod >> 12);
        }
    put32((uint8_t *)m + 16, (uint32_t)p22);
    return m;
}

void OuterProduct0(VECTOR *a, VECTOR *b, VECTOR *out) {
    uint32_t r0 = CFC(0), r2 = CFC(2), r4 = CFC(4);
    CTC(0, a->vx);
    CTC(2, a->vy);
    CTC(4, a->vz);
    MTC(9, b->vx);
    MTC(10, b->vy);
    MTC(11, b->vz);
    CMD(C_OP0);
    out->vx = (int32_t)MFC(25);
    out->vy = (int32_t)MFC(26);
    out->vz = (int32_t)MFC(27);
    CTC(0, r0);
    CTC(2, r2);
    CTC(4, r4);
}

/* tabla de seno y coseno (round(4096 * sin/cos(2*pi*i/4096))): exacta frente a la del
 * original (0x800B4F84, empaquetada seno | coseno << 16) */
static int16_t sct_s[4096], sct_c[4096];
static int sct_ready;

static void sc(int a, int32_t *s, int32_t *c) {
    if (!sct_ready) {
        for (int i = 0; i < 4096; i++) {
            sct_s[i] = (int16_t)lround(4096.0 * sin(i * (2.0 * M_PI / 4096.0)));
            sct_c[i] = (int16_t)lround(4096.0 * cos(i * (2.0 * M_PI / 4096.0)));
        }
        sct_ready = 1;
    }
    *s = sct_s[a & 0xFFF];
    *c = sct_c[a & 0xFFF];
}

MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m) {
    int32_t sx, cx, sy, cy, sz, cz;
    sc(r->vx, &sx, &cx);
    sc(r->vy, &sy, &cy);
    sc(r->vz, &sz, &cz);
    m->m[1][2] = (int16_t)-sx;
    m->m[0][2] = (int16_t)(sy * cx >> 12);
    m->m[2][2] = (int16_t)(cy * cx >> 12);
    m->m[1][0] = (int16_t)(sz * cx >> 12);
    m->m[1][1] = (int16_t)(cz * cx >> 12);
    int32_t t = sy * sx >> 12;
    m->m[0][0] = (int16_t)((cy * cz >> 12) + (t * sz >> 12));
    m->m[0][1] = (int16_t)((t * cz >> 12) - (cy * sz >> 12));
    t = cy * sx >> 12;
    m->m[2][1] = (int16_t)((sy * sz >> 12) + (t * cz >> 12));
    m->m[2][0] = (int16_t)((t * sz >> 12) - (sy * cz >> 12));
    return m;
}

MATRIX *RotMatrix(SVECTOR *r, MATRIX *m) {
    int32_t sx, cx, sy, cy, sz, cz;
    sc(r->vx, &sx, &cx);
    sc(r->vy, &sy, &cy);
    sc(r->vz, &sz, &cz);
    m->m[0][2] = (int16_t)sy;
    m->m[1][2] = (int16_t)(-(cy * sx) >> 12);
    m->m[2][2] = (int16_t)(cy * cx >> 12);
    m->m[0][0] = (int16_t)(cz * cy >> 12);
    m->m[0][1] = (int16_t)(-(sz * cy) >> 12);
    int32_t t = cz * -sy >> 12;
    m->m[1][0] = (int16_t)((sz * cx >> 12) - (t * sx >> 12));
    m->m[2][0] = (int16_t)((sz * sx >> 12) + (t * cx >> 12));
    t = sz * -sy >> 12;
    m->m[1][1] = (int16_t)((cz * cx >> 12) + (t * sx >> 12));
    m->m[2][1] = (int16_t)((cz * sx >> 12) - (t * cx >> 12));
    return m;
}

/* VectorNormalSS (0x800A4928 + ayudante 0x800A4958): SQR (lm), suma de cuadrados,
 * normalizacion por LZCS, IR0 = inverso de la raiz desde tabla, GPF y desplazamiento
 * (31 - (lzcr & ~1)) >> 1. Tabla: floor(32768 / sqrt(v)), v = 0x40..0xFF (exacta). */
static int16_t rsq_tbl[192];
static int rsq_ready;

int32_t VectorNormalSS(SVECTOR *v0, SVECTOR *v1) {
    if (!rsq_ready) {
        for (int v = 0x40; v < 0x100; v++) rsq_tbl[v - 0x40] = (int16_t)floor(32768.0 / sqrt((double)v));
        rsq_ready = 1;
    }
    int32_t x = v0->vx, y = v0->vy, z = v0->vz;
    MTC(9, (uint32_t)x);
    MTC(10, (uint32_t)y);
    MTC(11, (uint32_t)z);
    CMD(0x0A00428u);   /* SQR, sf = 0, lm = 1 */
    int32_t sum = (int32_t)((uint32_t)MFC(25) + (uint32_t)MFC(26) + (uint32_t)MFC(27));
    MTC(30, (uint32_t)sum);
    int32_t lz = (int32_t)(MFC(31) & ~1u);
    int32_t sh = (0x1F - lz) >> 1;
    int32_t k = lz - 0x18 >= 0 ? (int32_t)((uint32_t)sum << (lz - 0x18)) : sum >> (0x18 - lz);
    MTC(8, (uint32_t)(int32_t)rsq_tbl[(k - 0x40) & 0xFF]);
    MTC(9, (uint32_t)x);
    MTC(10, (uint32_t)y);
    MTC(11, (uint32_t)z);
    CMD(0x190003Du);   /* GPF, sf = 0 */
    v1->vx = (int16_t)((int32_t)MFC(25) >> sh);
    v1->vy = (int16_t)((int32_t)MFC(26) >> sh);
    v1->vz = (int16_t)((int32_t)MFC(27) >> sh);
    return sum;
}

/* TransMatrix (sin firma, 0x800A53A8 en EU): copia el vector en la traslacion */
MATRIX *TransMatrix(MATRIX *m, VECTOR *v) {
    m->t[0] = v->vx; m->t[1] = v->vy; m->t[2] = v->vz;
    return m;
}

/* CompMatrix (0x800A4C18): m2 = m0 * m1 (rotacion) y m2.t = m0.R * m1.t + m0.t, con las
 * peculiaridades del original: m1.t se carga truncada a 16 bits, y el SWC2 de IR3 escribe
 * los 32 bits (el relleno tras m2[2][2] queda con el signo extendido). */
MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2) {
    const uint8_t *a = (const uint8_t *)m0, *b = (const uint8_t *)m1;
    uint8_t *c = (uint8_t *)m2;
    for (int i = 0; i < 5; i++) CTC(i, w32(a + 4 * i));
    uint32_t ir[3][3];
    uint32_t vxy[3] = {(uint32_t)(w32(b) & 0xFFFF) | (w32(b + 4) & 0xFFFF0000u),
                       (uint32_t)(w32(b) >> 16) | (w32(b + 8) << 16),
                       (uint32_t)(w32(b + 4) & 0xFFFF) | (w32(b + 8) & 0xFFFF0000u)};
    uint32_t vz[3] = {w32(b + 12), (uint32_t)(int32_t)(int16_t)(w32(b + 12) >> 16), w32(b + 16)};
    for (int k = 0; k < 3; k++) {
        MTC(0, vxy[k]);
        MTC(1, vz[k]);
        CMD(C_RTV0);
        for (int r = 0; r < 3; r++) ir[k][r] = MFC(9 + r);
    }
    put32(c + 0, (ir[1][0] << 16) | (ir[0][0] & 0xFFFF));
    put32(c + 12, (ir[1][2] << 16) | (ir[0][2] & 0xFFFF));
    put32(c + 16, ir[2][2]);
    put32(c + 4, (ir[0][1] << 16) | (ir[2][0] & 0xFFFF));
    put32(c + 8, (ir[2][1] << 16) | (ir[1][1] & 0xFFFF));
    MTC(0, (w32(b + 20) & 0xFFFF) | (w32(b + 24) << 16));
    MTC(1, w32(b + 28));
    CMD(C_RTV0);
    for (int r = 0; r < 3; r++) put32(c + 20 + 4 * r, MFC(25 + r) + w32(a + 20 + 4 * r));
    return m2;
}

/* RotMatrix_gte (0x800A5E78): la misma matriz que RotMatrix pero con productos hechos por
 * la GTE (GPF, sf=1) en el mismo orden que el original, para que tambien coincida el
 * estado final de la GTE. Solo escribe 16 bits en m[2][2] (no toca el relleno). */
#define C_GPF 0x198003Du
static void gpf(int32_t ir0, int32_t a, int32_t b, int32_t c, int32_t *r1, int32_t *r2, int32_t *r3) {
    MTC(8, (uint32_t)ir0); MTC(9, (uint32_t)a); MTC(10, (uint32_t)b); MTC(11, (uint32_t)c);
    CMD(C_GPF);
    *r1 = (int32_t)MFC(9); *r2 = (int32_t)MFC(10); *r3 = (int32_t)MFC(11);
}
MATRIX *RotMatrix_gte(SVECTOR *r, MATRIX *m) {
    int32_t sx, cx, sy, cy, sz, cz;
    sc(r->vx < 0 ? -r->vx : r->vx, &sx, &cx); if (r->vx < 0) sx = -sx;
    sc(r->vy < 0 ? -r->vy : r->vy, &sy, &cy); if (r->vy < 0) sy = -sy;
    sc(r->vz < 0 ? -r->vz : r->vz, &sz, &cz); if (r->vz < 0) sz = -sz;
    sx = (int16_t)sx; sy = (int16_t)sy; sz = (int16_t)sz;
    int32_t cxsy, cxsz, cxcz, sxsy, sxsz, sxcz, czcy, czsxsy, czcxsy, szcy, szsxsy, szcxsy;
    gpf(cx, sy, sz, cz, &cxsy, &cxsz, &cxcz);
    int32_t cycx = cy * cx;
    gpf(sx, sy, sz, cz, &sxsy, &sxsz, &sxcz);
    m->m[2][2] = (int16_t)(cycx >> 12);
    int32_t cysx = cy * sx;
    gpf(cz, cy, sxsy, cxsy, &czcy, &czsxsy, &czcxsy);
    gpf(sz, cy, sxsy, cxsy, &szcy, &szsxsy, &szcxsy);
    m->m[0][2] = (int16_t)sy;
    m->m[1][0] = (int16_t)(czsxsy + cxsz);
    m->m[0][0] = (int16_t)czcy;
    m->m[0][1] = (int16_t)-szcy;
    m->m[1][1] = (int16_t)(cxcz - szsxsy);
    m->m[1][2] = (int16_t)-(cysx >> 12);
    m->m[2][0] = (int16_t)(sxsz - czcxsy);
    m->m[2][1] = (int16_t)(sxcz + szcxsy);
    return m;
}
