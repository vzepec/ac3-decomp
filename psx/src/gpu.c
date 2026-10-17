#include <stdio.h>
/* GPU por software segun psx-spx. Ver psx_gpu.h. */
#include "psx_gpu.h"

#include <string.h>

#include "psx_mem.h"

void psx_gpu_reset(PsxGpu *g) {
    memset(g, 0, sizeof *g);
    g->area_x2 = PSX_VRAM_W - 1;
    g->area_y2 = PSX_VRAM_H - 1;
}

static int sx11(uint32_t v) { return (int)((int32_t)(v << 21) >> 21); }   /* 11 bits con signo */

/* ---- pixeles ---- */

static const int DITHER[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

static int in_area(PsxGpu *g, int x, int y) {
    return x >= g->area_x1 && x <= g->area_x2 && y >= g->area_y1 && y <= g->area_y2 && x >= 0 && y >= 0 &&
           x < PSX_VRAM_W && y < PSX_VRAM_H;
}

/* escribe un pixel de 15 bits + bit de mascara, con semitransparencia si procede */
static void plot(PsxGpu *g, int x, int y, uint16_t c, int semi) {
    if (!in_area(g, x, y)) return;
    uint16_t *d = &g->vram[y * PSX_VRAM_W + x];
    if (g->mask_check && (*d & 0x8000)) return;
    if (semi) {
        int br = *d & 31, bg = (*d >> 5) & 31, bb = (*d >> 10) & 31;
        int fr = c & 31, fg = (c >> 5) & 31, fb = (c >> 10) & 31;
        int r, gg, b;
        switch (g->abr) {
        case 0: r = (br + fr) >> 1; gg = (bg + fg) >> 1; b = (bb + fb) >> 1; break;
        case 1: r = br + fr; gg = bg + fg; b = bb + fb; break;
        case 2: r = br - fr; gg = bg - fg; b = bb - fb; break;
        default: r = br + (fr >> 2); gg = bg + (fg >> 2); b = bb + (fb >> 2); break;
        }
        r = r < 0 ? 0 : r > 31 ? 31 : r;
        gg = gg < 0 ? 0 : gg > 31 ? 31 : gg;
        b = b < 0 ? 0 : b > 31 ? 31 : b;
        c = (uint16_t)((c & 0x8000) | r | gg << 5 | b << 10);
    }
    *d = (uint16_t)(c | (g->mask_set ? 0x8000 : 0));
    g->pixels_drawn++;
}

/* texel de la pagina actual (con ventana de textura); 0 = transparente */
static uint16_t texel(PsxGpu *g, int u, int v, int clut_x, int clut_y) {
    u &= 255;
    v &= 255;
    u = (u & ~(g->tw_mask_x * 8)) | ((g->tw_off_x & g->tw_mask_x) * 8);
    v = (v & ~(g->tw_mask_y * 8)) | ((g->tw_off_y & g->tw_mask_y) * 8);
    int bx = g->tp_x * 64, by = g->tp_y * 256;
    if (g->tp_depth == 0) {
        uint16_t w = g->vram[((by + v) & 511) * PSX_VRAM_W + ((bx + u / 4) & 1023)];
        int idx = (w >> ((u & 3) * 4)) & 15;
        return g->vram[clut_y * PSX_VRAM_W + ((clut_x + idx) & 1023)];
    }
    if (g->tp_depth == 1) {
        uint16_t w = g->vram[((by + v) & 511) * PSX_VRAM_W + ((bx + u / 2) & 1023)];
        int idx = (w >> ((u & 1) * 8)) & 255;
        return g->vram[clut_y * PSX_VRAM_W + ((clut_x + idx) & 1023)];
    }
    return g->vram[((by + v) & 511) * PSX_VRAM_W + ((bx + u) & 1023)];
}

/* color final de un pixel de primitiva: textura (modulada o en crudo), color, tramado */
static void shade_plot(PsxGpu *g, int x, int y, int r, int gg, int b, int textured, int raw, int u, int v,
                       int cx, int cy, int semi, int dither) {
    uint16_t out;
    int st = semi;
    if (textured) {
        uint16_t t = texel(g, u, v, cx, cy);
        if (t == 0) return;
        st = semi && (t & 0x8000);
        if (raw) out = t;
        else {
            int tr = t & 31, tg = (t >> 5) & 31, tb = (t >> 10) & 31;
            /* modulacion: texel * color / 128 (en 8 bits, con tramado antes de bajar a 5) */
            int R = (tr << 3) * r >> 7, G = (tg << 3) * gg >> 7, B = (tb << 3) * b >> 7;
            if (dither) { int d = DITHER[y & 3][x & 3]; R += d; G += d; B += d; }
            R = R < 0 ? 0 : R > 255 ? 255 : R;
            G = G < 0 ? 0 : G > 255 ? 255 : G;
            B = B < 0 ? 0 : B > 255 ? 255 : B;
            out = (uint16_t)((t & 0x8000) | (R >> 3) | (G >> 3) << 5 | (B >> 3) << 10);
        }
    } else {
        if (dither) { int d = DITHER[y & 3][x & 3]; r += d; gg += d; b += d; }
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        out = (uint16_t)((r >> 3) | (gg >> 3) << 5 | (b >> 3) << 10);
    }
    plot(g, x, y, out, st);
}

/* ---- triangulos ---- */

typedef struct { int x, y, r, g, b, u, v; } Vert;

static int64_t edge(const Vert *a, const Vert *b, int px, int py) {
    return (int64_t)(b->x - a->x) * (py - a->y) - (int64_t)(b->y - a->y) * (px - a->x);
}

/* arista "superior o izquierda" (se dibuja) en un triangulo orientado positivamente */
static int top_left(const Vert *a, const Vert *b) {
    return (a->y == b->y && b->x < a->x) || (b->y > a->y);
}

static void triangle(PsxGpu *g, Vert v0, Vert v1, Vert v2, int gouraud, int textured, int raw, int semi, int cx,
                     int cy) {
    int64_t area = edge(&v0, &v1, v2.x, v2.y);
    if (area == 0) return;
    if (area < 0) { Vert t = v1; v1 = v2; v2 = t; area = -area; }
    int minx = v0.x, maxx = v0.x, miny = v0.y, maxy = v0.y;
    const Vert *vs[3] = {&v0, &v1, &v2};
    for (int i = 1; i < 3; i++) {
        if (vs[i]->x < minx) minx = vs[i]->x;
        if (vs[i]->x > maxx) maxx = vs[i]->x;
        if (vs[i]->y < miny) miny = vs[i]->y;
        if (vs[i]->y > maxy) maxy = vs[i]->y;
    }
    if (maxx - minx >= 1024 || maxy - miny >= 512) return;   /* el hardware descarta estos */
    if (minx < g->area_x1) minx = g->area_x1;
    if (miny < g->area_y1) miny = g->area_y1;
    if (maxx > g->area_x2) maxx = g->area_x2;
    if (maxy > g->area_y2) maxy = g->area_y2;
    /* regla de relleno de la PS1: se dibujan los bordes superior e izquierdo, no los derecho
     * e inferior (con y hacia abajo, la orientacion "positiva" es la horaria) */
    int b0 = top_left(&v1, &v2) ? -1 : 0, b1 = top_left(&v2, &v0) ? -1 : 0, b2 = top_left(&v0, &v1) ? -1 : 0;
    int dither = g->dither && (gouraud || (textured && !raw));
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            int64_t w0 = edge(&v1, &v2, x, y), w1 = edge(&v2, &v0, x, y), w2 = edge(&v0, &v1, x, y);
            if (w0 + b0 < 0 || w1 + b1 < 0 || w2 + b2 < 0) continue;
            int r = v0.r, gg = v0.g, b = v0.b, u = 0, v = 0;
            if (gouraud) {
                r = (int)((w0 * v0.r + w1 * v1.r + w2 * v2.r) / area);
                gg = (int)((w0 * v0.g + w1 * v1.g + w2 * v2.g) / area);
                b = (int)((w0 * v0.b + w1 * v1.b + w2 * v2.b) / area);
            }
            if (textured) {
                u = (int)((w0 * v0.u + w1 * v1.u + w2 * v2.u) / area);
                v = (int)((w0 * v0.v + w1 * v1.v + w2 * v2.v) / area);
            }
            shade_plot(g, x, y, r, gg, b, textured, raw, u, v, cx, cy, semi, dither);
        }
    g->prims_drawn++;
}

/* ---- comandos ---- */

static int cmd_length(uint32_t w) {
    int op = w >> 24;
    if (op >= 0x20 && op < 0x40) {
        int n = (op & 0x08) ? 4 : 3, tex = (op & 0x04) ? 1 : 0, gour = (op & 0x10) ? 1 : 0;
        return 1 + n * (1 + tex) + (gour ? n - 1 : 0);
    }
    if (op >= 0x40 && op < 0x60) return (op & 0x08) ? 2 : ((op & 0x10) ? 4 : 3);  /* polilinea: abierta */
    if (op >= 0x60 && op < 0x80) {
        int size = (op >> 3) & 3, tex = (op & 0x04) ? 1 : 0;
        return 2 + tex + (size == 0 ? 1 : 0);
    }
    if (op == 0x02) return 3;
    if (op >= 0x80 && op < 0xA0) return 4;
    if (op >= 0xA0 && op < 0xE0) return 3;
    return 1;
}

static void set_texpage(PsxGpu *g, uint32_t t) {
    g->tp_x = t & 15;
    g->tp_y = (t >> 4) & 1;
    g->abr = (t >> 5) & 3;
    g->tp_depth = (t >> 7) & 3;
    if (g->tp_depth == 3) g->tp_depth = 2;
}

static void do_polygon(PsxGpu *g, const uint32_t *c) {
    int op = c[0] >> 24;
    int quad = op & 0x08, tex = op & 0x04, semi = op & 0x02, raw = op & 0x01, gour = op & 0x10;
    int n = quad ? 4 : 3;
    Vert v[4];
    int k = 0, cx = 0, cy = 0;
    uint32_t col = c[k++] & 0xFFFFFF;
    for (int i = 0; i < n; i++) {
        if (i > 0 && gour) col = c[k++] & 0xFFFFFF;
        uint32_t xy = c[k++];
        v[i].x = sx11(xy) + g->off_x;
        v[i].y = sx11(xy >> 16) + g->off_y;
        v[i].r = col & 255; v[i].g = (col >> 8) & 255; v[i].b = (col >> 16) & 255;
        v[i].u = v[i].v = 0;
        if (tex) {
            uint32_t uv = c[k++];
            v[i].u = uv & 255;
            v[i].v = (uv >> 8) & 255;
            if (i == 0) { cx = ((uv >> 16) & 63) * 16; cy = (uv >> 22) & 511; }
            if (i == 1) set_texpage(g, uv >> 16);   /* el poligono texturizado fija la pagina */
        }
    }
    if (!gour) for (int i = 1; i < n; i++) { v[i].r = v[0].r; v[i].g = v[0].g; v[i].b = v[0].b; }
    triangle(g, v[0], v[1], v[2], gour, tex, raw, semi, cx, cy);
    if (quad) triangle(g, v[1], v[2], v[3], gour, tex, raw, semi, cx, cy);
}

static void line(PsxGpu *g, Vert a, Vert b, int gour, int semi) {
    int dx = b.x - a.x, dy = b.y - a.y;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx >= 1024 || ady >= 512) return;
    int steps = adx > ady ? adx : ady;
    for (int i = 0; i <= steps; i++) {
        int x = steps ? a.x + (dx * i + (dx >= 0 ? steps / 2 : -steps / 2)) / steps : a.x;
        int y = steps ? a.y + (dy * i + (dy >= 0 ? steps / 2 : -steps / 2)) / steps : a.y;
        int r = a.r, gg = a.g, bb = a.b;
        if (gour && steps) { r = a.r + (b.r - a.r) * i / steps; gg = a.g + (b.g - a.g) * i / steps; bb = a.b + (b.b - a.b) * i / steps; }
        shade_plot(g, x, y, r, gg, bb, 0, 0, 0, 0, 0, 0, semi, g->dither && gour);
    }
    g->prims_drawn++;
}

static void do_line(PsxGpu *g, const uint32_t *c) {
    int op = c[0] >> 24, gour = op & 0x10, semi = op & 0x02;
    Vert a, b;
    uint32_t c0 = c[0] & 0xFFFFFF, c1 = gour ? c[2] & 0xFFFFFF : c0;
    uint32_t p0 = c[1], p1 = gour ? c[3] : c[2];
    a.x = sx11(p0) + g->off_x; a.y = sx11(p0 >> 16) + g->off_y;
    b.x = sx11(p1) + g->off_x; b.y = sx11(p1 >> 16) + g->off_y;
    a.r = c0 & 255; a.g = (c0 >> 8) & 255; a.b = (c0 >> 16) & 255;
    b.r = c1 & 255; b.g = (c1 >> 8) & 255; b.b = (c1 >> 16) & 255;
    line(g, a, b, gour, semi);
}

static void do_rect(PsxGpu *g, const uint32_t *c) {
    int op = c[0] >> 24, tex = op & 0x04, semi = op & 0x02, raw = op & 0x01, size = (op >> 3) & 3;
    uint32_t col = c[0] & 0xFFFFFF;
    int x = sx11(c[1]) + g->off_x, y = sx11(c[1] >> 16) + g->off_y;
    int k = 2, u0 = 0, v0 = 0, cx = 0, cy = 0, w, h;
    if (tex) {
        uint32_t uv = c[k++];
        u0 = uv & 255; v0 = (uv >> 8) & 255;
        cx = ((uv >> 16) & 63) * 16; cy = (uv >> 22) & 511;
    }
    if (size == 0) { w = c[k] & 0x3FF; h = (c[k] >> 16) & 0x1FF; }
    else if (size == 1) w = h = 1;
    else if (size == 2) w = h = 8;
    else w = h = 16;
    int r = col & 255, gg = (col >> 8) & 255, b = (col >> 16) & 255;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            shade_plot(g, x + i, y + j, r, gg, b, tex, raw, u0 + i, v0 + j, cx, cy, semi, 0);
    g->prims_drawn++;
}

void psx_gpu_fill(PsxGpu *g, int x, int y, int w, int h, uint8_t r, uint8_t gr, uint8_t b) {
    uint16_t c = (uint16_t)((r >> 3) | (gr >> 3) << 5 | (b >> 3) << 10);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) g->vram[((y + j) & 511) * PSX_VRAM_W + ((x + i) & 1023)] = c;
}

static void execute(PsxGpu *g, const uint32_t *c) {
    int op = c[0] >> 24;
    if (op >= 0x20 && op < 0x40) { do_polygon(g, c); return; }
    if (op >= 0x40 && op < 0x60) { do_line(g, c); return; }
    if (op >= 0x60 && op < 0x80) { do_rect(g, c); return; }
    switch (op) {
    case 0x02: {   /* relleno: x a multiplos de 16, ignora area y mascara */
        uint32_t col = c[0] & 0xFFFFFF;
        int x = c[1] & 0x3F0, y = (c[1] >> 16) & 0x1FF;
        int w = ((c[2] & 0x3FF) + 15) & ~15, h = (c[2] >> 16) & 0x1FF;
        psx_gpu_fill(g, x, y, w, h, col & 255, (col >> 8) & 255, (col >> 16) & 255);
        break;
    }
    case 0xE1:
        set_texpage(g, c[0]);
        g->dither = (c[0] >> 9) & 1;
        g->draw_display = (c[0] >> 10) & 1;
        g->tex_disable = (c[0] >> 11) & 1;
        break;
    case 0xE2:
        g->tw_mask_x = c[0] & 31; g->tw_mask_y = (c[0] >> 5) & 31;
        g->tw_off_x = (c[0] >> 10) & 31; g->tw_off_y = (c[0] >> 15) & 31;
        break;
    case 0xE3: g->area_x1 = c[0] & 0x3FF; g->area_y1 = (c[0] >> 10) & 0x3FF; break;
    case 0xE4: g->area_x2 = c[0] & 0x3FF; g->area_y2 = (c[0] >> 10) & 0x3FF; break;
    case 0xE5: g->off_x = sx11(c[0]); g->off_y = sx11(c[0] >> 11); break;
    case 0xE6: g->mask_set = c[0] & 1; g->mask_check = (c[0] >> 1) & 1; break;
    default:
        if (op >= 0x80 && op < 0xA0) {   /* VRAM -> VRAM */
            int sx = c[1] & 1023, sy = (c[1] >> 16) & 511, dx = c[2] & 1023, dy = (c[2] >> 16) & 511;
            int w = ((c[3] - 1) & 1023) + 1, h = (((c[3] >> 16) - 1) & 511) + 1;
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w; i++)
                    g->vram[((dy + j) & 511) * PSX_VRAM_W + ((dx + i) & 1023)] =
                        g->vram[((sy + j) & 511) * PSX_VRAM_W + ((sx + i) & 1023)];
        } else if (op >= 0xA0 && op < 0xC0) {   /* CPU -> VRAM: siguen los datos */
            g->xfer_x = c[1] & 1023; g->xfer_y = (c[1] >> 16) & 511;
            g->xfer_w = ((c[2] - 1) & 1023) + 1; g->xfer_h = (((c[2] >> 16) - 1) & 511) + 1;
            g->xfer_pos = 0;
            g->xfer_active = 1;
        }
        break;
    }
}

static void xfer_pixel(PsxGpu *g, uint16_t p) {
    int i = g->xfer_pos % g->xfer_w, j = g->xfer_pos / g->xfer_w;
    g->vram[((g->xfer_y + j) & 511) * PSX_VRAM_W + ((g->xfer_x + i) & 1023)] = p;
    if (++g->xfer_pos >= g->xfer_w * g->xfer_h) g->xfer_active = 0;
}

void psx_gpu_gp0(PsxGpu *g, uint32_t w) {
    if (g->xfer_active) {
        xfer_pixel(g, (uint16_t)w);
        if (g->xfer_active) xfer_pixel(g, (uint16_t)(w >> 16));
        return;
    }
    if (g->poly_line) {   /* polilinea: vertices (y colores) hasta 0x5xxx5xxx */
        if ((w & 0xF000F000u) == 0x50005000u && g->cmd_len >= 2) { g->poly_line = 0; g->cmd_len = 0; return; }
        g->cmd[g->cmd_len++] = w;
        int gour = (g->cmd[0] >> 24) & 0x10;
        int need = gour ? 4 : 3;
        if (g->cmd_len == need) {
            uint32_t c[4];
            memcpy(c, g->cmd, sizeof c);
            if (!gour) c[2] = g->cmd[2];
            do_line(g, c);
            /* el ultimo vertice pasa a ser el primero del siguiente segmento */
            if (gour) { g->cmd[0] = (g->cmd[0] & 0xFF000000u) | (g->cmd[2] & 0xFFFFFF); g->cmd[1] = g->cmd[3]; g->cmd_len = 2; }
            else { g->cmd[1] = g->cmd[2]; g->cmd_len = 2; }
        }
        return;
    }
    if (g->cmd_len == 0) {
        g->cmd_need = cmd_length(w);
        if ((w >> 24) >= 0x48 && (w >> 24) < 0x60 && ((w >> 24) & 0x08)) {   /* polilinea */
            g->cmd[0] = w;
            g->cmd_len = 1;
            g->poly_line = 1;
            return;
        }
    }
    g->cmd[g->cmd_len++] = w;
    if (g->cmd_len >= g->cmd_need) {
        execute(g, g->cmd);
        g->cmd_len = 0;
    }
}

void psx_gpu_draw_list(PsxGpu *g, uint32_t addr) {
    /* una lista corrupta (o ciclica) colgaria la consola; aqui se corta (un fotograma real no
     * llega a 10.000 paquetes) */
    int guard = 0;
    while ((addr & 0xFFFFFF) != 0xFFFFFF) {
        if (++guard > 200000) {
            static int warned;
            if (!warned++) fprintf(stderr, "gpu: lista de dibujo de mas de 200.000 paquetes: se corta\n");
            break;
        }
        const uint32_t *p = (const uint32_t *)psx_ptr(addr);
        uint32_t tag = p[0];
        int n = tag >> 24;
        for (int i = 1; i <= n; i++) psx_gpu_gp0(g, p[i]);
        addr = 0x80000000u | (tag & 0xFFFFFF);
    }
}

void psx_gpu_load_image(PsxGpu *g, int x, int y, int w, int h, const uint16_t *src) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) g->vram[((y + j) & 511) * PSX_VRAM_W + ((x + i) & 1023)] = src[j * w + i];
}

void psx_gpu_store_image(PsxGpu *g, int x, int y, int w, int h, uint16_t *dst) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) dst[j * w + i] = g->vram[((y + j) & 511) * PSX_VRAM_W + ((x + i) & 1023)];
}
