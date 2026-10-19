/* Emulador de la GTE segun psx-spx (nocash). Ver psx_gte.h.
 * Convenciones: los registros se guardan en crudo (32 bits) y se interpretan al
 * leerlos; cada orden empieza con FLAG = 0 y va acumulando banderas. */
#include "psx_gte.h"

#include <string.h>

/* ---- banderas (registro de control 31) ---- */
#define F_MAC1_POS (1u << 30)
#define F_MAC2_POS (1u << 29)
#define F_MAC3_POS (1u << 28)
#define F_MAC1_NEG (1u << 27)
#define F_MAC2_NEG (1u << 26)
#define F_MAC3_NEG (1u << 25)
#define F_IR1 (1u << 24)
#define F_IR2 (1u << 23)
#define F_IR3 (1u << 22)
#define F_R (1u << 21)
#define F_G (1u << 20)
#define F_B (1u << 19)
#define F_SZ (1u << 18)
#define F_DIV (1u << 17)
#define F_MAC0_POS (1u << 16)
#define F_MAC0_NEG (1u << 15)
#define F_SX2 (1u << 14)
#define F_SY2 (1u << 13)
#define F_IR0 (1u << 12)
#define F_ERROR_MASK 0x7F87E000u   /* bits que activan el bit 31 */

static uint8_t unr_table[0x101];
static int unr_ready;

/* Tabla de la division UNR (psx-spx): max(0, (0x40000/(i+0x100)+1)/2 - 0x101). */
static void unr_init(void) {
    for (int i = 0; i <= 0x100; i++) {
        int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
        unr_table[i] = (uint8_t)(v < 0 ? 0 : v);
    }
    unr_ready = 1;
}

void psx_gte_reset(PsxGte *g) {
    memset(g, 0, sizeof *g);
    if (!unr_ready) unr_init();
}

/* ---- acceso a campos ---- */
static int16_t lo16(int32_t v) { return (int16_t)(v & 0xFFFF); }
static int16_t hi16(int32_t v) { return (int16_t)((uint32_t)v >> 16); }
#define FLAG (g->c[31])

static int16_t vx(PsxGte *g, int n) { return lo16(g->d[2 * n]); }
static int16_t vy(PsxGte *g, int n) { return hi16(g->d[2 * n]); }
static int16_t vz(PsxGte *g, int n) { return lo16(g->d[2 * n + 1]); }
static int16_t ir(PsxGte *g, int n) { return lo16(g->d[8 + n]); }   /* n = 0..3 */
static void set_ir(PsxGte *g, int n, int32_t v) { g->d[8 + n] = (int16_t)v; }

/* elemento (i, j) de una matriz de 3x3 empaquetada en 5 palabras desde `base` */
static int16_t mat(PsxGte *g, int base, int i, int j) {
    int k = i * 3 + j;
    int32_t w = g->c[base + k / 2];
    return (k & 1) ? hi16(w) : lo16(w);
}

/* ---- saturaciones ---- */
static int64_t A(PsxGte *g, int i, int64_t v) {      /* MAC1-3: 44 bits con signo */
    if (v > 0x7FFFFFFFFFFLL) FLAG |= (i == 1 ? F_MAC1_POS : i == 2 ? F_MAC2_POS : F_MAC3_POS);
    else if (v < -0x80000000000LL) FLAG |= (i == 1 ? F_MAC1_NEG : i == 2 ? F_MAC2_NEG : F_MAC3_NEG);
    return (int64_t)((uint64_t)v << 20) >> 20;       /* extension de signo desde el bit 43 */
}

static int32_t F(PsxGte *g, int64_t v) {             /* MAC0: 32 bits */
    if (v > 0x7FFFFFFFLL) FLAG |= F_MAC0_POS;
    else if (v < -0x80000000LL) FLAG |= F_MAC0_NEG;
    return (int32_t)v;
}

static int32_t lm_b(PsxGte *g, int i, int32_t v, int lm) {
    int32_t lo = lm ? 0 : -0x8000;
    uint32_t f = i == 1 ? F_IR1 : i == 2 ? F_IR2 : F_IR3;
    if (v < lo) { FLAG |= f; return lo; }
    if (v > 0x7FFF) { FLAG |= f; return 0x7FFF; }
    return v;
}

static int32_t lm_d(PsxGte *g, int64_t v) {          /* SZ3, OTZ */
    if (v < 0) { FLAG |= F_SZ; return 0; }
    if (v > 0xFFFF) { FLAG |= F_SZ; return 0xFFFF; }
    return (int32_t)v;
}

static int32_t lm_g(PsxGte *g, int32_t v, uint32_t f) { /* SX2, SY2 */
    if (v < -0x400) { FLAG |= f; return -0x400; }
    if (v > 0x3FF) { FLAG |= f; return 0x3FF; }
    return v;
}

static int32_t lm_h(PsxGte *g, int32_t v) {          /* IR0 */
    if (v < 0) { FLAG |= F_IR0; return 0; }
    if (v > 0x1000) { FLAG |= F_IR0; return 0x1000; }
    return v;
}

static int32_t lm_c(PsxGte *g, int32_t v, uint32_t f) { /* colores */
    if (v < 0) { FLAG |= f; return 0; }
    if (v > 0xFF) { FLAG |= f; return 0xFF; }
    return v;
}

static void set_mac(PsxGte *g, int i, int64_t v, int shift) {
    g->d[24 + i] = (int32_t)(A(g, i, v) >> shift);
}

static void set_mac_ir(PsxGte *g, int i, int64_t v, int shift, int lm) {
    set_mac(g, i, v, shift);
    set_ir(g, i, lm_b(g, i, g->d[24 + i], lm));
}

static void push_color(PsxGte *g) {
    uint32_t code = (uint32_t)g->d[6] & 0xFF000000u;
    g->d[20] = g->d[21];
    g->d[21] = g->d[22];
    g->d[22] = (int32_t)(code | (uint32_t)lm_c(g, g->d[25] >> 4, F_R) | (uint32_t)lm_c(g, g->d[26] >> 4, F_G) << 8 |
                         (uint32_t)lm_c(g, g->d[27] >> 4, F_B) << 16);
}

static void push_sz(PsxGte *g, int32_t z) {
    g->d[16] = g->d[17];
    g->d[17] = g->d[18];
    g->d[18] = g->d[19];
    g->d[19] = z;
}

static void push_sxy(PsxGte *g, int32_t x, int32_t y) {
    g->d[12] = g->d[13];
    g->d[13] = g->d[14];
    g->d[14] = (int32_t)(((uint32_t)y << 16) | ((uint32_t)x & 0xFFFF));
}

/* ---- division UNR (psx-spx, "GTE Division Inaccuracy") ---- */
static uint32_t unr_core(uint32_t h, uint32_t sz3);

static uint32_t unr_divide(PsxGte *g, uint32_t h, uint32_t sz3) {
    if (h >= sz3 * 2) { FLAG |= F_DIV; return 0x1FFFF; }
    return unr_core(h, sz3);
}

uint32_t psx_gte_unr_divide(uint32_t h, uint32_t sz3) {
    if (!unr_ready) unr_init();
    if (h >= sz3 * 2) return 0x1FFFF;
    return unr_core(h, sz3);
}

static uint32_t unr_core(uint32_t h, uint32_t sz3) {
    int z = 0;
    while (z < 16 && !(sz3 & (0x8000u >> z))) z++;
    uint64_t n = (uint64_t)h << z;
    uint32_t d = sz3 << z;
    uint32_t u = unr_table[(d - 0x7FC0) >> 7] + 0x101u;
    d = (uint32_t)((0x2000080u - (uint64_t)d * u) >> 8);
    d = (uint32_t)((0x0000080u + (uint64_t)d * u) >> 8);
    uint64_t q = (n * d + 0x8000) >> 16;
    return q > 0x1FFFF ? 0x1FFFF : (uint32_t)q;
}

/* fila i (1-3) de matriz*vector + traslacion, con comprobacion de 44 bits en cada suma */
static int64_t row_dot(PsxGte *g, int i, int64_t base, int16_t m0, int16_t m1, int16_t m2, int32_t x, int32_t y,
                       int32_t z) {
    int64_t s = A(g, i, base);
    s = A(g, i, s + (int64_t)m0 * x);
    s = A(g, i, s + (int64_t)m1 * y);
    return A(g, i, s + (int64_t)m2 * z);
}

/* ---- RTPS / RTPT ---- */
void (*psx_gte_vertex_hook)(int32_t x, int32_t y, int32_t z, int32_t sx, int32_t sy, uint32_t grp);
float psx_gte_wide = 1.0f;
static void rtps(PsxGte *g, int n, int shift, int lm, int last) {
    int32_t x = vx(g, n), y = vy(g, n), z = vz(g, n);
    int64_t s3 = 0;
    for (int i = 1; i <= 3; i++) {
        int64_t s = row_dot(g, i, (int64_t)g->c[4 + i] << 12, mat(g, 0, i - 1, 0), mat(g, 0, i - 1, 1),
                            mat(g, 0, i - 1, 2), x, y, z);
        g->d[24 + i] = (int32_t)(s >> shift);
        if (i < 3) set_ir(g, i, lm_b(g, i, g->d[24 + i], lm));
        else s3 = s;
    }
    /* IR3: se satura con el valor desplazado, pero la bandera mira MAC3 >> 12 (psx-spx) */
    int32_t m3 = g->d[27], lo = lm ? 0 : -0x8000;
    set_ir(g, 3, m3 < lo ? lo : m3 > 0x7FFF ? 0x7FFF : m3);
    int64_t t = s3 >> 12;
    if (t < -0x8000 || t > 0x7FFF) FLAG |= F_IR3;
    push_sz(g, lm_d(g, s3 >> 12));
    uint32_t h = (uint32_t)g->c[26] & 0xFFFF;
    int64_t q = unr_divide(g, h, (uint32_t)g->d[19]);
    /* SX/SY salen del valor de 64 bits desplazado; MAC0 solo comprueba el desbordamiento */
    int64_t px = (int64_t)g->c[24] + ir(g, 1) * q, py = (int64_t)g->c[25] + ir(g, 2) * q;
    if (psx_gte_wide != 1.0f) px = (int64_t)g->c[24] + (int64_t)((double)(ir(g, 1) * q) * psx_gte_wide);
    F(g, px);
    g->d[24] = F(g, py);                                  /* MAC0 queda con el ultimo calculo */
    int64_t tx = px >> 16, ty = py >> 16;
    int32_t sx = lm_g(g, tx < -0x8000 ? -0x8000 : tx > 0x7FFF ? 0x7FFF : (int32_t)tx, F_SX2);
    int32_t sy = lm_g(g, ty < -0x8000 ? -0x8000 : ty > 0x7FFF ? 0x7FFF : (int32_t)ty, F_SY2);
    push_sxy(g, sx, sy);
    if (psx_gte_vertex_hook) {   /* interpolacion de imagenes */
        uint32_t grp = 2166136261u;   /* el objeto: rotacion (c0-c4) y traslacion (c5-c7) */
        for (int i = 0; i < 8; i++) grp = (grp ^ (uint32_t)g->c[i]) * 16777619u;
        psx_gte_vertex_hook(x, y, z, sx, sy, grp);
    }
    if (last) {
        /* IR0 se satura desde el valor completo, no desde MAC0 truncado a 32 bits: con el vertice
         * muy cerca (q grande) DQA * q desborda MAC0 y, truncado, IR0 saldria 0x1000 (niebla
         * total) en vez de 0 (comparado con la GTE de Mednafen, psx/test/gteref.c) */
        int16_t dqa = lo16(g->c[27]);
        int64_t dq = (int64_t)g->c[28] + (int64_t)dqa * q;
        g->d[24] = F(g, dq);
        set_ir(g, 0, lm_h(g, (int32_t)(dq >> 12)));
    }
}

/* ---- interpolacion hacia el color lejano (DPCS, DPCT, INTPL, DCPL, NCDS...) ---- */
static void interpolate(PsxGte *g, int64_t m1, int64_t m2, int64_t m3, int shift, int lm) {
    int64_t in[3] = {m1, m2, m3};
    for (int i = 1; i <= 3; i++) set_mac_ir(g, i, ((int64_t)g->c[20 + i] << 12) - in[i - 1], shift, 0);
    for (int i = 1; i <= 3; i++) set_mac_ir(g, i, (int64_t)ir(g, i) * ir(g, 0) + in[i - 1], shift, lm);
    push_color(g);
}

static int32_t rgbc(PsxGte *g, int k) { return ((uint32_t)g->d[6] >> (8 * k)) & 0xFF; }

/* ---- iluminacion: paso 1 (matriz de luz) y paso 2 (color de luz + fondo) ---- */
static void light_step1(PsxGte *g, int n, int shift, int lm) {
    for (int i = 1; i <= 3; i++)
        set_mac_ir(g, i, row_dot(g, i, 0, mat(g, 8, i - 1, 0), mat(g, 8, i - 1, 1), mat(g, 8, i - 1, 2), vx(g, n),
                                 vy(g, n), vz(g, n)), shift, lm);
}

static void light_step2(PsxGte *g, int shift, int lm) {
    int32_t a = ir(g, 1), b = ir(g, 2), c = ir(g, 3);
    for (int i = 1; i <= 3; i++)
        set_mac_ir(g, i, row_dot(g, i, (int64_t)g->c[12 + i] << 12, mat(g, 16, i - 1, 0), mat(g, 16, i - 1, 1),
                                 mat(g, 16, i - 1, 2), a, b, c), shift, lm);
}

static void color_step_cc(PsxGte *g, int shift, int lm) {   /* NCCS / CC */
    for (int i = 1; i <= 3; i++) set_mac_ir(g, i, ((int64_t)rgbc(g, i - 1) * ir(g, i)) << 4, shift, lm);
    push_color(g);
}

static void color_step_cd(PsxGte *g, int shift, int lm) {   /* NCDS / CDP */
    interpolate(g, ((int64_t)rgbc(g, 0) * ir(g, 1)) << 4, ((int64_t)rgbc(g, 1) * ir(g, 2)) << 4,
                ((int64_t)rgbc(g, 2) * ir(g, 3)) << 4, shift, lm);
}

/* ---- MVMVA ---- */
static void mvmva(PsxGte *g, uint32_t cmd, int shift, int lm) {
    int mx = (cmd >> 17) & 3, vv = (cmd >> 15) & 3, cv = (cmd >> 13) & 3;
    int32_t v[3];
    if (vv < 3) { v[0] = vx(g, vv); v[1] = vy(g, vv); v[2] = vz(g, vv); }
    else { v[0] = ir(g, 1); v[1] = ir(g, 2); v[2] = ir(g, 3); }
    int16_t m[3][3];
    if (mx < 3) {
        int base = mx == 0 ? 0 : mx == 1 ? 8 : 16;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) m[i][j] = mat(g, base, i, j);
    } else {   /* matriz "reservada": comportamiento del hardware (psx-spx) */
        int16_t r = (int16_t)(rgbc(g, 0) << 4);
        m[0][0] = (int16_t)-r; m[0][1] = r; m[0][2] = ir(g, 0);
        m[1][0] = m[1][1] = m[1][2] = mat(g, 0, 0, 2);
        m[2][0] = m[2][1] = m[2][2] = mat(g, 0, 1, 1);
    }
    int32_t t[3] = {0, 0, 0};
    if (cv == 0) { t[0] = g->c[5]; t[1] = g->c[6]; t[2] = g->c[7]; }
    else if (cv == 1) { t[0] = g->c[13]; t[1] = g->c[14]; t[2] = g->c[15]; }
    else if (cv == 2) { t[0] = g->c[21]; t[1] = g->c[22]; t[2] = g->c[23]; }
    for (int i = 1; i <= 3; i++) {
        if (cv == 2) {
            /* fallo del hardware con FC: la primera columna solo afecta a las banderas */
            int64_t f = A(g, i, A(g, i, (int64_t)t[i - 1] << 12) + (int64_t)m[i - 1][0] * v[0]);
            lm_b(g, i, (int32_t)(f >> shift), 0);
            int64_t s = A(g, i, (int64_t)m[i - 1][1] * v[1]);
            s = A(g, i, s + (int64_t)m[i - 1][2] * v[2]);
            set_mac_ir(g, i, s, shift, lm);
        } else {
            set_mac_ir(g, i, row_dot(g, i, (int64_t)t[i - 1] << 12, m[i - 1][0], m[i - 1][1], m[i - 1][2], v[0], v[1],
                                     v[2]), shift, lm);
        }
    }
}

void (*psx_gte_cmd_hook)(PsxGte *g, uint32_t cmd, int after);
static void command(PsxGte *g, uint32_t cmd);
void psx_gte_command(PsxGte *g, uint32_t cmd) {
    if (psx_gte_cmd_hook) {
        psx_gte_cmd_hook(g, cmd, 0);
        command(g, cmd);
        psx_gte_cmd_hook(g, cmd, 1);
    } else command(g, cmd);
}
static void command(PsxGte *g, uint32_t cmd) {
    if (!unr_ready) unr_init();
    int shift = (cmd & GTE_SF) ? 12 : 0;
    int lm = (cmd & GTE_LM) ? 1 : 0;
    FLAG = 0;
    switch (cmd & 0x3F) {
    case GTE_RTPS: rtps(g, 0, shift, lm, 1); break;
    case GTE_RTPT: rtps(g, 0, shift, lm, 0); rtps(g, 1, shift, lm, 0); rtps(g, 2, shift, lm, 1); break;
    case GTE_NCLIP: {
        int32_t x0 = lo16(g->d[12]), y0 = hi16(g->d[12]), x1 = lo16(g->d[13]), y1 = hi16(g->d[13]);
        int32_t x2 = lo16(g->d[14]), y2 = hi16(g->d[14]);
        g->d[24] = F(g, (int64_t)x0 * y1 + (int64_t)x1 * y2 + (int64_t)x2 * y0 - (int64_t)x0 * y2 - (int64_t)x1 * y0 -
                            (int64_t)x2 * y1);
        break;
    }
    case GTE_AVSZ3:
        g->d[24] = F(g, (int64_t)lo16(g->c[29]) * ((g->d[17] & 0xFFFF) + (g->d[18] & 0xFFFF) + (g->d[19] & 0xFFFF)));
        g->d[7] = lm_d(g, (int64_t)g->d[24] >> 12);
        break;
    case GTE_AVSZ4:
        g->d[24] = F(g, (int64_t)lo16(g->c[30]) *
                            ((g->d[16] & 0xFFFF) + (g->d[17] & 0xFFFF) + (g->d[18] & 0xFFFF) + (g->d[19] & 0xFFFF)));
        g->d[7] = lm_d(g, (int64_t)g->d[24] >> 12);
        break;
    case GTE_MVMVA: mvmva(g, cmd, shift, lm); break;
    case GTE_SQR:
        for (int i = 1; i <= 3; i++) set_mac_ir(g, i, (int64_t)ir(g, i) * ir(g, i), shift, lm);
        break;
    case GTE_OP: {
        int32_t d1 = mat(g, 0, 0, 0), d2 = mat(g, 0, 1, 1), d3 = mat(g, 0, 2, 2);
        int32_t a = ir(g, 1), b = ir(g, 2), c = ir(g, 3);
        set_mac(g, 1, (int64_t)c * d2 - (int64_t)b * d3, shift);
        set_mac(g, 2, (int64_t)a * d3 - (int64_t)c * d1, shift);
        set_mac(g, 3, (int64_t)b * d1 - (int64_t)a * d2, shift);
        for (int i = 1; i <= 3; i++) set_ir(g, i, lm_b(g, i, g->d[24 + i], lm));
        break;
    }
    case GTE_GPF:
        for (int i = 1; i <= 3; i++) set_mac_ir(g, i, (int64_t)ir(g, 0) * ir(g, i), shift, lm);
        push_color(g);
        break;
    case GTE_GPL:
        for (int i = 1; i <= 3; i++)
            set_mac_ir(g, i, ((int64_t)g->d[24 + i] << shift) + (int64_t)ir(g, 0) * ir(g, i), shift, lm);
        push_color(g);
        break;
    case GTE_DPCS:
        interpolate(g, (int64_t)rgbc(g, 0) << 16, (int64_t)rgbc(g, 1) << 16, (int64_t)rgbc(g, 2) << 16, shift, lm);
        break;
    case GTE_DPCT:
        for (int k = 0; k < 3; k++) {
            uint32_t c0 = (uint32_t)g->d[20];
            interpolate(g, (int64_t)(c0 & 0xFF) << 16, (int64_t)((c0 >> 8) & 0xFF) << 16,
                        (int64_t)((c0 >> 16) & 0xFF) << 16, shift, lm);
        }
        break;
    case GTE_INTPL:
        interpolate(g, (int64_t)ir(g, 1) << 12, (int64_t)ir(g, 2) << 12, (int64_t)ir(g, 3) << 12, shift, lm);
        break;
    case GTE_DCPL:
        interpolate(g, ((int64_t)rgbc(g, 0) * ir(g, 1)) << 4, ((int64_t)rgbc(g, 1) * ir(g, 2)) << 4,
                    ((int64_t)rgbc(g, 2) * ir(g, 3)) << 4, shift, lm);
        break;
    case GTE_NCS: light_step1(g, 0, shift, lm); light_step2(g, shift, lm); push_color(g); break;
    case GTE_NCT:
        for (int n = 0; n < 3; n++) { light_step1(g, n, shift, lm); light_step2(g, shift, lm); push_color(g); }
        break;
    case GTE_NCCS: light_step1(g, 0, shift, lm); light_step2(g, shift, lm); color_step_cc(g, shift, lm); break;
    case GTE_NCCT:
        for (int n = 0; n < 3; n++) { light_step1(g, n, shift, lm); light_step2(g, shift, lm); color_step_cc(g, shift, lm); }
        break;
    case GTE_NCDS: light_step1(g, 0, shift, lm); light_step2(g, shift, lm); color_step_cd(g, shift, lm); break;
    case GTE_NCDT:
        for (int n = 0; n < 3; n++) { light_step1(g, n, shift, lm); light_step2(g, shift, lm); color_step_cd(g, shift, lm); }
        break;
    case GTE_CC: light_step2(g, shift, lm); color_step_cc(g, shift, lm); break;
    case GTE_CDP: light_step2(g, shift, lm); color_step_cd(g, shift, lm); break;
    default: break;   /* ordenes no definidas: el hardware hace cosas raras; no las usa el juego */
    }
    if (FLAG & F_ERROR_MASK) FLAG |= (int32_t)0x80000000u;
}

/* ---- acceso a registros ---- */
static uint32_t orgb(PsxGte *g) {
    uint32_t out = 0;
    for (int i = 0; i < 3; i++) {
        int32_t v = ir(g, i + 1) >> 7;
        out |= (uint32_t)(v < 0 ? 0 : v > 0x1F ? 0x1F : v) << (5 * i);
    }
    return out;
}

uint32_t psx_gte_read_data(PsxGte *g, int r) {
    switch (r) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        return (uint32_t)(int32_t)lo16(g->d[r]);
    case 7: case 16: case 17: case 18: case 19:
        return (uint32_t)g->d[r] & 0xFFFF;
    case 15: return (uint32_t)g->d[14];
    case 28: case 29: return orgb(g);
    default: return (uint32_t)g->d[r];
    }
}

void psx_gte_write_data(PsxGte *g, int r, uint32_t v) {
    switch (r) {
    case 15: push_sxy(g, lo16((int32_t)v), hi16((int32_t)v)); break;
    case 28:
        g->d[28] = (int32_t)(v & 0x7FFF);
        set_ir(g, 1, (int32_t)(v & 0x1F) << 7);
        set_ir(g, 2, (int32_t)((v >> 5) & 0x1F) << 7);
        set_ir(g, 3, (int32_t)((v >> 10) & 0x1F) << 7);
        break;
    case 29: case 31: break;   /* solo lectura */
    case 30: {
        g->d[30] = (int32_t)v;
        uint32_t u = (v & 0x80000000u) ? ~v : v;
        int n = 0;
        while (n < 32 && !(u & (0x80000000u >> n))) n++;
        g->d[31] = n;
        break;
    }
    default: g->d[r] = (int32_t)v; break;
    }
}

uint32_t psx_gte_read_ctrl(PsxGte *g, int r) {
    switch (r) {
    case 4: case 12: case 20: case 26: case 27: case 29: case 30:
        return (uint32_t)(int32_t)lo16(g->c[r]);   /* H tambien se lee con signo (fallo del hardware) */
    default: return (uint32_t)g->c[r];
    }
}

void psx_gte_write_ctrl(PsxGte *g, int r, uint32_t v) {
    if (r == 31) {
        v &= 0x7FFFF000u;
        if (v & F_ERROR_MASK) v |= 0x80000000u;
    }
    g->c[r] = (int32_t)v;
}
