#include <stdio.h>
#include <stdlib.h>
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

/* CLUT x y base y de la pagina del triangulo en curso (por hilo: las imagenes intermedias se
 * dibujan en hilos aparte, cada uno con su PsxGpu) */
static _Thread_local int g_tri_cx, g_tri_by;
static const int DITHER[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

/* registro de primitivas (ver psx_gpu.h) */
static _Thread_local PsxHwList *g_rec;
static _Thread_local int g_rec_only;

/* etapa 8, paso 2: zonas de la VRAM que el software no tiene al dia (imagenes que, con only, solo
 * dibuja la GPU). Por hilo: solo las lleva el del juego (psx_gpu_draw_list) */
#define NSTALE 4
static _Thread_local int g_st_n;
static _Thread_local int16_t g_st[NSTALE][4];
static _Thread_local int g_pend, g_px1, g_py1, g_px2, g_py2;   /* escrituras fuera de las listas en esas zonas */
long psx_gpu_stale_ev[4];
static int stale_hit(int x1, int y1, int x2, int y2) {
    for (int i = 0; i < g_st_n; i++)
        if (x2 >= g_st[i][0] && x1 <= g_st[i][2] && y2 >= g_st[i][1] && y1 <= g_st[i][3]) return 1;
    return 0;
}
/* la lista anotada lee (como textura) esas zonas: se apuntan en ella para que la GPU las tenga en su VRAM */
static void stale_read(int x1, int y1, int x2, int y2) {
    for (int i = 0; i < g_st_n; i++) {
        if (!(x2 >= g_st[i][0] && x1 <= g_st[i][2] && y2 >= g_st[i][1] && y1 <= g_st[i][3])) continue;
        if (!g_rec) continue;
        int k = 0;
        while (k < g_rec->nread && memcmp(g_rec->read[k], g_st[i], sizeof g_st[i])) k++;
        if (k == g_rec->nread && k < 4) { memcpy(g_rec->read[k], g_st[i], sizeof g_st[i]); g_rec->nread++; }
    }
}
int psx_gpu_main_stale(int x1, int y1, int x2, int y2) { return stale_hit(x1, y1, x2, y2); }
void psx_gpu_main_mark(int x1, int y1, int x2, int y2, int stale) {
    int k = 0;
    for (int i = 0; i < g_st_n; i++)   /* fuera las que se solapan: el software las acaba de dibujar (o se sustituyen) */
        if (!(x2 >= g_st[i][0] && x1 <= g_st[i][2] && y2 >= g_st[i][1] && y1 <= g_st[i][3])) memcpy(g_st[k++], g_st[i], sizeof g_st[i]);
    g_st_n = k;
    if (stale && g_st_n < NSTALE) {
        g_st[g_st_n][0] = (int16_t)x1; g_st[g_st_n][1] = (int16_t)y1; g_st[g_st_n][2] = (int16_t)x2; g_st[g_st_n][3] = (int16_t)y2;
        g_st_n++;
    }
}
/* escritura fuera de una lista (LoadImage, ClearImage, MoveImage) en una zona que solo tiene la GPU:
 * se le pasa con la siguiente lista anotada (subida y copia a la imagen) */
static void stale_write(int x1, int y1, int x2, int y2) {
    if (g_rec || !g_st_n || !stale_hit(x1, y1, x2, y2)) return;
    psx_gpu_stale_ev[3]++;
    if (!g_pend) { g_px1 = x1; g_py1 = y1; g_px2 = x2; g_py2 = y2; g_pend = 1; return; }
    if (x1 < g_px1) g_px1 = x1;
    if (y1 < g_py1) g_py1 = y1;
    if (x2 > g_px2) g_px2 = x2;
    if (y2 > g_py2) g_py2 = y2;
}
/* textura (o paleta) leida de una zona que solo tiene la GPU: el software la tiene vieja */
static void stale_tex(const PsxHwPrim *p, int nv) {
    if (!g_st_n || !(p->flags & PSX_HWF_TEX)) return;
    int u1 = 255, u2 = 0, v1 = 255, v2 = 0;
    if (p->kind == PSX_HW_RECT) {
        u1 = p->u[0]; v1 = p->v[0];
        u2 = u1 + p->x[1] - 1; v2 = v1 + p->y[1] - 1;
        if (u2 > 255) u1 = 0, u2 = 255;
        if (v2 > 255) v1 = 0, v2 = 255;
    } else
        for (int i = 0; i < nv; i++) {
            if (p->u[i] < u1) u1 = p->u[i];
            if (p->u[i] > u2) u2 = p->u[i];
            if (p->v[i] < v1) v1 = p->v[i];
            if (p->v[i] > v2) v2 = p->v[i];
        }
    int sh = p->depth == 0 ? 2 : p->depth == 1 ? 1 : 0;
    if (getenv("PSX_STALE_DBG") && psx_gpu_stale_ev[0] < 12 && (stale_hit(p->tp_x + (u1 >> sh), p->tp_y + v1, p->tp_x + (u2 >> sh), p->tp_y + v2) ||
        (p->depth < 2 && stale_hit(p->clut_x, p->clut_y, p->clut_x + (p->depth ? 255 : 15), p->clut_y)))) {
        int VSync(int);
        fprintf(stderr, "gpu: VBlank %d, textura de la imagen de la GPU: tipo %d prof %d pagina %d,%d uv %d-%d,%d-%d clut %d,%d en (%d,%d); zonas:",
                VSync(-1), p->kind, p->depth, p->tp_x, p->tp_y, u1, u2, v1, v2, p->clut_x, p->clut_y, p->x[0], p->y[0]);
        for (int i = 0; i < g_st_n; i++) fprintf(stderr, " (%d,%d)-(%d,%d)", g_st[i][0], g_st[i][1], g_st[i][2], g_st[i][3]);
        fprintf(stderr, "\n");
    }
    if (stale_hit(p->tp_x + (u1 >> sh), p->tp_y + v1, p->tp_x + (u2 >> sh), p->tp_y + v2)) {
        psx_gpu_stale_ev[0]++;
        stale_read(p->tp_x + (u1 >> sh), p->tp_y + v1, p->tp_x + (u2 >> sh), p->tp_y + v2);
    } else if (p->depth < 2 && stale_hit(p->clut_x, p->clut_y, p->clut_x + (p->depth ? 255 : 15), p->clut_y)) {
        psx_gpu_stale_ev[0]++;
        stale_read(p->clut_x, p->clut_y, p->clut_x + (p->depth ? 255 : 15), p->clut_y);
    }
}

static void rec_dirty(int x1, int y1, int x2, int y2);
/* etapa 8, paso 7: decimales de las proyecciones (ver psx_gpu.h). Direccionamiento abierto por el valor
 * entero; generacion para vaciarla sin borrar */
int psx_gpu_subpix_on;
#define SPBITS 17
typedef struct { uint32_t key, gen; uint8_t fx, fy, amb; } SubPix;   /* amb: dos vertices distintos con ese entero */
static _Thread_local SubPix *g_sp;
static _Thread_local uint32_t g_sp_gen = 1;
int psx_gpu_subpix_hits, psx_gpu_subpix_miss, psx_gpu_subpix_amb;   /* medicion */
static int g_sp_noamb = -1;   /* prueba: PSX_SUBPIX_NOAMB=1, sin marcar los ambiguos (el primero manda, como antes) */
void psx_gpu_subpix_store(uint32_t sxy, uint32_t fx16, uint32_t fy16) {
    if (g_sp_noamb < 0) g_sp_noamb = getenv("PSX_SUBPIX_NOAMB") != NULL;
    if (!g_sp && !(g_sp = calloc((size_t)1 << SPBITS, sizeof *g_sp))) return;
    uint32_t h = (sxy * 2654435761u) >> (32 - SPBITS);
    for (int n = 0; n < 32; n++, h = (h + 1) & ((1u << SPBITS) - 1)) {
        SubPix *e = &g_sp[h];
        if (e->gen != g_sp_gen) { e->gen = g_sp_gen; e->key = sxy; e->fx = (uint8_t)(fx16 >> 8); e->fy = (uint8_t)(fy16 >> 8); e->amb = 0; return; }
        /* ya esta: el mismo vertice da los mismos decimales. Otros decimales son de otro vertice que cae en el
         * mismo pixel: ese entero queda sin decimales (antes mandaba el primero, que cambia de una imagen a otra:
         * las texturas de los bordes en 16:9 temblaban en la Retroid, con muchos vertices lejanos juntos; etapa 8,
         * paso 10). La misma respuesta para todos los que lo usan: sin grietas */
        if (e->key == sxy) {
            if (!g_sp_noamb && (e->fx != (uint8_t)(fx16 >> 8) || e->fy != (uint8_t)(fy16 >> 8))) { e->amb = 1; psx_gpu_subpix_amb++; }
            return;
        }
    }
}
static int subpix_get(uint32_t sxy, int *fx, int *fy) {
    *fx = *fy = 0;
    if (!g_sp) return 0;
    uint32_t h = (sxy * 2654435761u) >> (32 - SPBITS);
    for (int n = 0; n < 32; n++, h = (h + 1) & ((1u << SPBITS) - 1)) {
        SubPix *e = &g_sp[h];
        if (e->gen != g_sp_gen) return 0;
        if (e->key == sxy) {
            if (e->amb) return 0;
            *fx = e->fx; *fy = e->fy;
            return 1;
        }
    }
    return 0;
}

void psx_gpu_record(PsxHwList *list, int only) {
    if (!list) g_sp_gen++;   /* fin de una lista: las proyecciones siguientes son de otra imagen */
    g_rec = list;
    g_rec_only = list && only;
    if (list) list->dirty = list->blit = list->nread = 0;
    if (list && g_pend) {   /* lo escrito fuera de las listas en la imagen de la GPU, al principio de esta */
        g_pend = 0;
        rec_dirty(g_px1, g_py1, g_px2, g_py2);
        list->blit = 1;
    }
}
/* rectangulo (inclusivo) totalmente fuera del area principal */
static int rec_off(int x1, int y1, int x2, int y2) {
    const PsxHwList *l = g_rec;
    return x2 < l->main_x1 || x1 > l->main_x2 || y2 < l->main_y1 || y1 > l->main_y2;
}
static void rec_dirty(int x1, int y1, int x2, int y2) {
    PsxHwList *l = g_rec;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > 1023) x2 = 1023;
    if (y2 > 511) y2 = 511;
    if (x2 < x1 || y2 < y1) return;
    if (!l->dirty) { l->dx1 = x1; l->dy1 = y1; l->dx2 = x2; l->dy2 = y2; l->dirty = 1; return; }
    if (x1 < l->dx1) l->dx1 = x1;
    if (y1 < l->dy1) l->dy1 = y1;
    if (x2 > l->dx2) l->dx2 = x2;
    if (y2 > l->dy2) l->dy2 = y2;
}
static PsxHwPrim *rec_raw(void) {
    if (!g_rec || g_rec->n >= g_rec->max) return NULL;
    return &g_rec->p[g_rec->n++];
}
/* la zona sucia pasa a una UPLOAD con el contenido actual de la VRAM del software */
static void rec_flush_dirty(PsxGpu *g) {
    PsxHwList *l = g_rec;
    if (!l->dirty) return;
    int w = l->dx2 - l->dx1 + 1, h = l->dy2 - l->dy1 + 1;
    l->dirty = 0;
    if (l->ndata + w * h > l->maxdata) return;
    PsxHwPrim *p = rec_raw();
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->kind = PSX_HW_UPLOAD;
    p->x[0] = (int16_t)l->dx1; p->y[0] = (int16_t)l->dy1; p->x[1] = (int16_t)w; p->y[1] = (int16_t)h;
    p->data_off = (uint32_t)l->ndata;
    for (int j = 0; j < h; j++) memcpy(l->data + l->ndata + j * w, &g->vram[(l->dy1 + j) * PSX_VRAM_W + l->dx1], (size_t)w * 2);
    l->ndata += w * h;
    if (l->blit) {   /* carga en la imagen: ademas se dibuja desde la textura, ya actualizada */
        l->blit = 0;
        PsxHwPrim *q = rec_raw();
        if (!q) return;
        *q = *p;
        q->kind = PSX_HW_VRAM;
        q->clut_x = (uint16_t)p->x[0]; q->clut_y = (uint16_t)p->y[0];
        q->ax1 = 0; q->ay1 = 0; q->ax2 = -1; q->ay2 = -1;
    }
}
void psx_gpu_record_flush(PsxGpu *g) { if (g_rec) rec_flush_dirty(g); }
static PsxHwPrim *rec_new(PsxGpu *g, int kind, int flags) {
    if (!g_rec || g_rec->n >= g_rec->max) return NULL;
    rec_flush_dirty(g);
    PsxHwPrim *p = rec_raw();
    if (!p) return NULL;
    memset(p, 0, sizeof *p);
    p->kind = (uint8_t)kind;
    p->flags = (uint8_t)(flags | (g->mask_set ? PSX_HWF_MASKSET : 0) | (g->mask_check ? PSX_HWF_MASKCHK : 0));
    p->abr = (uint8_t)g->abr;
    p->depth = (uint8_t)g->tp_depth;
    p->tp_x = (uint16_t)(g->tp_x * 64); p->tp_y = (uint16_t)(g->tp_y * 256);
    p->tw_mx = (uint8_t)g->tw_mask_x; p->tw_my = (uint8_t)g->tw_mask_y;
    p->tw_ox = (uint8_t)g->tw_off_x; p->tw_oy = (uint8_t)g->tw_off_y;
    p->ax1 = (int16_t)g->area_x1; p->ay1 = (int16_t)g->area_y1; p->ax2 = (int16_t)g->area_x2; p->ay2 = (int16_t)g->area_y2;
    p->ox = (int16_t)g->off_x; p->oy = (int16_t)g->off_y;
    return p;
}

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

/* ---- tramo de triangulo optimizado: mismo resultado que shade_plot + plot, sin las
 * comprobaciones que el recorte del triangulo ya garantiza ---- */
#define AINLINE static inline __attribute__((always_inline))

typedef struct { const uint16_t *page; const uint16_t *clut; int umask, uor, vmask, vor, depth, bx; } TexCtx;

static void tex_ctx(PsxGpu *g, TexCtx *t, int cx, int cy) {
    t->umask = ~(g->tw_mask_x * 8); t->uor = (g->tw_off_x & g->tw_mask_x) * 8;
    t->vmask = ~(g->tw_mask_y * 8); t->vor = (g->tw_off_y & g->tw_mask_y) * 8;
    t->depth = g->tp_depth;
    t->bx = g->tp_x * 64;
    t->page = g->vram + (g->tp_y * 256) * PSX_VRAM_W;
    t->clut = g->vram + cy * PSX_VRAM_W;
    t->bx |= 0;
    (void)cx;
}

AINLINE uint16_t texel_fast(PsxGpu *g, const TexCtx *t, int u, int v, int cx, int by) {
    u = ((u & 255) & t->umask) | t->uor;
    v = ((v & 255) & t->vmask) | t->vor;
    const uint16_t *row = g->vram + ((by + v) & 511) * PSX_VRAM_W;
    if (t->depth == 0) {
        uint16_t w = row[(t->bx + u / 4) & 1023];
        return t->clut[(cx + ((w >> ((u & 3) * 4)) & 15)) & 1023];
    }
    if (t->depth == 1) {
        uint16_t w = row[(t->bx + u / 2) & 1023];
        return t->clut[(cx + ((w >> ((u & 1) * 8)) & 255)) & 1023];
    }
    return row[(t->bx + u) & 1023];
}

AINLINE int put_px(uint16_t *d, uint16_t c, int semi, int mask_check, uint16_t mask_set, int abr) {
    if (mask_check && (*d & 0x8000)) return 0;
    if (semi) {
        int br = *d & 31, bg = (*d >> 5) & 31, bb = (*d >> 10) & 31;
        int fr = c & 31, fg = (c >> 5) & 31, fb = (c >> 10) & 31;
        int r, gg, b;
        switch (abr) {
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
    *d = (uint16_t)(c | mask_set);
    return 1;
}

AINLINE void tri_span(PsxGpu *g, const TexCtx *tc, int y, int xl, int xr, const int32_t *q0, const int32_t *r0,
                      const int32_t *dq, const int32_t *dr, int32_t area, const int gouraud, const int textured, int raw,
                      int semi, int dither, int fr, int fg, int fb) {
    const int na = (gouraud ? 3 : 0) + (textured ? 2 : 0), t0 = gouraud ? 3 : 0;
    int32_t q[5], r[5];
    for (int k = 0; k < 5; k++) { q[k] = q0[k]; r[k] = r0[k]; }
    const int mask_check = g->mask_check, abr = g->abr, ccx = g_tri_cx, cby = g_tri_by;
    const uint16_t mask_set = g->mask_set ? 0x8000 : 0;
    uint32_t drawn = 0;
    uint16_t *d = g->vram + y * PSX_VRAM_W + xl;
    const int *dith = DITHER[y & 3];
    for (int x = xl; x <= xr; x++, d++) {
        int rr = gouraud ? q[0] : fr, gg = gouraud ? q[1] : fg, bb = gouraud ? q[2] : fb;
        uint16_t out;
        int st = semi;
        int skip = 0;
        if (textured) {
            uint16_t t = texel_fast(g, tc, q[t0], q[t0 + 1], ccx, cby);
            if (t == 0) skip = 1;
            else {
                st = semi && (t & 0x8000);
                if (raw) out = t;
                else {
                    int tr = t & 31, tg = (t >> 5) & 31, tb = (t >> 10) & 31;
                    int R = (tr << 3) * rr >> 7, G = (tg << 3) * gg >> 7, B = (tb << 3) * bb >> 7;
                    if (dither) { int dd = dith[x & 3]; R += dd; G += dd; B += dd; }
                    R = R < 0 ? 0 : R > 255 ? 255 : R;
                    G = G < 0 ? 0 : G > 255 ? 255 : G;
                    B = B < 0 ? 0 : B > 255 ? 255 : B;
                    out = (uint16_t)((t & 0x8000) | (R >> 3) | (G >> 3) << 5 | (B >> 3) << 10);
                }
            }
        } else {
            if (dither) { int dd = dith[x & 3]; rr += dd; gg += dd; bb += dd; }
            rr = rr < 0 ? 0 : rr > 255 ? 255 : rr;
            gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
            bb = bb < 0 ? 0 : bb > 255 ? 255 : bb;
            out = (uint16_t)((rr >> 3) | (gg >> 3) << 5 | (bb >> 3) << 10);
        }
        if (!skip) drawn += (uint32_t)put_px(d, out, st, mask_check, mask_set, abr);
        for (int k = 0; k < na; k++) {
            q[k] += dq[k];
            r[k] += dr[k];
            int c = r[k] >= area;
            q[k] += c;
            r[k] -= c ? area : 0;
        }
    }
    g->pixels_drawn += drawn;
}

/* ---- triangulos ---- */

typedef struct { int x, y, r, g, b, u, v, fx, fy; } Vert;   /* fx, fy: decimales (1/256 px, paso 7) */

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
    if (g_rec && rec_off(g->area_x1, g->area_y1, g->area_x2, g->area_y2)) {
        /* fuera de la imagen (paletas y texturas que dibuja la lista): se dibuja en la VRAM del
         * software y se sube a la textura antes de la siguiente primitiva de la imagen */
        rec_dirty(minx > g->area_x1 ? minx : g->area_x1, miny > g->area_y1 ? miny : g->area_y1,
                  maxx < g->area_x2 ? maxx : g->area_x2, maxy < g->area_y2 ? maxy : g->area_y2);
    } else if (g_rec) {
        PsxHwPrim *p = rec_new(g, PSX_HW_TRI, (textured ? PSX_HWF_TEX : 0) | (raw ? PSX_HWF_RAW : 0) | (semi ? PSX_HWF_SEMI : 0) |
                                                  (gouraud ? PSX_HWF_GOURAUD : 0) | (g->dither && (gouraud || (textured && !raw)) ? PSX_HWF_DITHER : 0));
        if (p) {
            const Vert *w[3] = {&v0, &v1, &v2};
            for (int i = 0; i < 3; i++) {
                p->x[i] = (int16_t)w[i]->x; p->y[i] = (int16_t)w[i]->y;
                p->r[i] = (uint8_t)w[i]->r; p->g[i] = (uint8_t)w[i]->g; p->b[i] = (uint8_t)w[i]->b;
                p->u[i] = (uint8_t)w[i]->u; p->v[i] = (uint8_t)w[i]->v;
                p->fx[i] = (uint8_t)w[i]->fx; p->fy[i] = (uint8_t)w[i]->fy;
            }
            p->clut_x = (uint16_t)cx; p->clut_y = (uint16_t)cy;
            stale_tex(p, 3);
        }
        if (g_rec_only) return;
    }
    if (minx < g->area_x1) minx = g->area_x1;
    if (miny < g->area_y1) miny = g->area_y1;
    if (maxx > g->area_x2) maxx = g->area_x2;
    if (maxy > g->area_y2) maxy = g->area_y2;
    /* regla de relleno de la PS1: se dibujan los bordes superior e izquierdo, no los derecho
     * e inferior (con y hacia abajo, la orientacion "positiva" es la horaria) */
    int b0 = top_left(&v1, &v2) ? -1 : 0, b1 = top_left(&v2, &v0) ? -1 : 0, b2 = top_left(&v0, &v1) ? -1 : 0;
    /* lo que in_area rechazaria pixel a pixel: fuera de la VRAM */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > PSX_VRAM_W - 1) maxx = PSX_VRAM_W - 1;
    if (maxy > PSX_VRAM_H - 1) maxy = PSX_VRAM_H - 1;
    int dither = g->dither && (gouraud || (textured && !raw));
    /* recorrido incremental: las aristas y los numeradores de los atributos son lineales en x;
     * cada atributo se lleva como cociente + resto de su division por el area, asi que el valor
     * es exactamente (w0*a0 + w1*a1 + w2*a2) / area, como la formula directa */
    int64_t d0 = -(int64_t)(v2.y - v1.y), d1 = -(int64_t)(v0.y - v2.y), d2 = -(int64_t)(v1.y - v0.y);
    int na = 0;
    int64_t at[5][3];
    if (gouraud) {
        at[na][0] = v0.r; at[na][1] = v1.r; at[na][2] = v2.r; na++;
        at[na][0] = v0.g; at[na][1] = v1.g; at[na][2] = v2.g; na++;
        at[na][0] = v0.b; at[na][1] = v1.b; at[na][2] = v2.b; na++;
    }
    if (textured) {
        at[na][0] = v0.u; at[na][1] = v1.u; at[na][2] = v2.u; na++;
        at[na][0] = v0.v; at[na][1] = v1.v; at[na][2] = v2.v; na++;
    }
    int32_t dq[5] = {0}, dr[5] = {0};
    for (int k = 0; k < na; k++) {
        int64_t dn = d0 * at[k][0] + d1 * at[k][1] + d2 * at[k][2];
        int64_t q = dn / area;
        if (dn % area < 0) q--;   /* division por abajo */
        dq[k] = (int32_t)q;
        dr[k] = (int32_t)(dn - q * area);
    }
    TexCtx tc = {0};
    if (textured) { tex_ctx(g, &tc, cx, cy); g_tri_cx = cx; g_tri_by = g->tp_y * 256; }
    /* todo incremental tambien en y: aristas en x = minx y cociente + resto de cada atributo en
     * x = minx; al principio de cada tramo se avanza (xl - minx) pasos con una division */
    int64_t ey[3] = {v2.x - v1.x, v0.x - v2.x, v1.x - v0.x};
    int64_t e[3] = {edge(&v1, &v2, minx, miny) + b0, edge(&v2, &v0, minx, miny) + b1, edge(&v0, &v1, minx, miny) + b2};
    int64_t dd[3] = {d0, d1, d2};
    int32_t qm[5] = {0}, rm[5] = {0}, dqy[5] = {0}, dry[5] = {0};
    {
        int64_t w0 = edge(&v1, &v2, minx, miny), w1 = edge(&v2, &v0, minx, miny), w2 = edge(&v0, &v1, minx, miny);
        for (int k = 0; k < na; k++) {
            int64_t n = w0 * at[k][0] + w1 * at[k][1] + w2 * at[k][2];
            int64_t qq = n / area;
            if (n % area < 0) qq--;
            qm[k] = (int32_t)qq;
            rm[k] = (int32_t)(n - qq * area);
            int64_t dn = ey[0] * at[k][0] + ey[1] * at[k][1] + ey[2] * at[k][2];
            qq = dn / area;
            if (dn % area < 0) qq--;
            dqy[k] = (int32_t)qq;
            dry[k] = (int32_t)(dn - qq * area);
        }
    }
    int32_t a32 = (int32_t)area;
    for (int y = miny; y <= maxy; y++) {
        /* tramo de la fila: x tal que e_i + d_i * (x - minx) >= 0 para las tres aristas */
        int xl = minx, xr = maxx;
        for (int k = 0; k < 3 && xl <= xr; k++) {
            if (dd[k] == 0) { if (e[k] < 0) xl = xr + 1; continue; }
            if (dd[k] > 0) {
                if (e[k] < 0) { int64_t t = (-e[k] + dd[k] - 1) / dd[k]; if (minx + t > xl) xl = (int)(minx + t > maxx + 1 ? maxx + 1 : minx + t); }
            } else {
                if (e[k] < 0) { xl = xr + 1; continue; }
                int64_t t = e[k] / -dd[k];
                if (minx + t < xr) xr = (int)(minx + t);
            }
        }
        if (xl <= xr) {
            int32_t q[5] = {0}, r[5] = {0};
            int64_t kx = xl - minx;
            for (int k = 0; k < na; k++) {
                int64_t t = rm[k] + kx * dr[k];   /* >= 0 */
                q[k] = (int32_t)(qm[k] + kx * dq[k] + t / area);
                r[k] = (int32_t)(t % area);
            }
            /* versiones especializadas del tramo (las banderas son constantes en cada llamada) */
            if (gouraud && textured) tri_span(g, &tc, y, xl, xr, q, r, dq, dr, a32, 1, 1, raw, semi, dither, v0.r, v0.g, v0.b);
            else if (gouraud) tri_span(g, &tc, y, xl, xr, q, r, dq, dr, a32, 1, 0, raw, semi, dither, v0.r, v0.g, v0.b);
            else if (textured) tri_span(g, &tc, y, xl, xr, q, r, dq, dr, a32, 0, 1, raw, semi, dither, v0.r, v0.g, v0.b);
            else tri_span(g, &tc, y, xl, xr, q, r, dq, dr, a32, 0, 0, raw, semi, dither, v0.r, v0.g, v0.b);
        }
        for (int k = 0; k < 3; k++) e[k] += ey[k];
        for (int k = 0; k < na; k++) {
            qm[k] += dqy[k];
            rm[k] += dry[k];
            if (rm[k] >= a32) { rm[k] -= a32; qm[k]++; }
        }
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
        v[i].fx = v[i].fy = 0;
        if (psx_gpu_subpix_on && g_rec) {
            if (subpix_get(xy, &v[i].fx, &v[i].fy)) psx_gpu_subpix_hits++;
            else psx_gpu_subpix_miss++;
        }
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
    if (g_rec && rec_off(g->area_x1, g->area_y1, g->area_x2, g->area_y2)) {
        rec_dirty(g->area_x1, g->area_y1, g->area_x2, g->area_y2);
    } else if (g_rec) {
        PsxHwPrim *p = rec_new(g, PSX_HW_LINE, (semi ? PSX_HWF_SEMI : 0) | (gour ? PSX_HWF_GOURAUD : 0) | (g->dither && gour ? PSX_HWF_DITHER : 0));
        if (p) {
            p->x[0] = (int16_t)a.x; p->y[0] = (int16_t)a.y; p->x[1] = (int16_t)b.x; p->y[1] = (int16_t)b.y;
            p->r[0] = (uint8_t)a.r; p->g[0] = (uint8_t)a.g; p->b[0] = (uint8_t)a.b;
            p->r[1] = (uint8_t)b.r; p->g[1] = (uint8_t)b.g; p->b[1] = (uint8_t)b.b;
        }
        if (g_rec_only) return;
    }
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
    if (g_rec && (rec_off(g->area_x1, g->area_y1, g->area_x2, g->area_y2) || rec_off(x, y, x + w - 1, y + h - 1))) {
        rec_dirty(x, y, x + w - 1, y + h - 1);
    } else if (g_rec) {
        PsxHwPrim *p = rec_new(g, PSX_HW_RECT, (tex ? PSX_HWF_TEX : 0) | (raw ? PSX_HWF_RAW : 0) | (semi ? PSX_HWF_SEMI : 0));
        if (p) {
            p->x[0] = (int16_t)x; p->y[0] = (int16_t)y; p->x[1] = (int16_t)w; p->y[1] = (int16_t)h;
            p->r[0] = (uint8_t)r; p->g[0] = (uint8_t)gg; p->b[0] = (uint8_t)b;
            p->u[0] = (uint8_t)u0; p->v[0] = (uint8_t)v0;
            p->clut_x = (uint16_t)cx; p->clut_y = (uint16_t)cy;
            stale_tex(p, 1);
        }
        if (g_rec_only) return;
    }
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            shade_plot(g, x + i, y + j, r, gg, b, tex, raw, u0 + i, v0 + j, cx, cy, semi, 0);
    g->prims_drawn++;
}

void psx_gpu_fill(PsxGpu *g, int x, int y, int w, int h, uint8_t r, uint8_t gr, uint8_t b) {
    stale_write(x, y, x + w - 1, y + h - 1);
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
        if (g_rec && rec_off(x, y, x + w - 1, y + h - 1)) {
            rec_dirty(x, y, x + w - 1, y + h - 1);
        } else if (g_rec) {
            PsxHwPrim *p = rec_new(g, PSX_HW_FILL, 0);
            if (p) {
                p->x[0] = (int16_t)x; p->y[0] = (int16_t)y; p->x[1] = (int16_t)w; p->y[1] = (int16_t)h;
                p->r[0] = (uint8_t)(col & 255); p->g[0] = (uint8_t)((col >> 8) & 255); p->b[0] = (uint8_t)((col >> 16) & 255);
            }
            if (g_rec_only) break;
        }
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
            /* (en una lista que dibuja entera el software, copiar desde su propia area lee lo ya dibujado: bien) */
            int own = g_rec && !g_rec_only && sx >= g->area_x1 && sx + w - 1 <= g->area_x2 && sy >= g->area_y1 && sy + h - 1 <= g->area_y2;
            if (g_st_n && !own && stale_hit(sx, sy, sx + w - 1, sy + h - 1)) {
                psx_gpu_stale_ev[1]++;
                stale_read(sx, sy, sx + w - 1, sy + h - 1);
                if (getenv("PSX_STALE_DBG") && psx_gpu_stale_ev[1] < 20) {
                    int VSync(int);
                    fprintf(stderr, "gpu: VBlank %d, copia desde (%d,%d) %dx%d a (%d,%d), en lista %s, area (%d,%d)-(%d,%d)\n", VSync(-1), sx, sy, w, h, dx, dy,
                            g_rec ? (g_rec_only ? "solo GPU" : "entera") : "ninguna", g->area_x1, g->area_y1, g->area_x2, g->area_y2);
                }
            }
            stale_write(dx, dy, dx + w - 1, dy + h - 1);
            if (g_rec && rec_off(dx, dy, dx + w - 1, dy + h - 1)) {
                rec_dirty(dx, dy, dx + w - 1, dy + h - 1);   /* p. ej. las paletas de la bruma */
            } else if (g_rec) {   /* copia a la imagen: se dibuja desde el origen (u, v) */
                PsxHwPrim *p = rec_new(g, PSX_HW_VRAM, 0);
                if (p) { p->x[0] = (int16_t)dx; p->y[0] = (int16_t)dy; p->x[1] = (int16_t)w; p->y[1] = (int16_t)h;
                         p->clut_x = (uint16_t)sx; p->clut_y = (uint16_t)sy; }
                if (g_rec_only) break;
            }
            for (int j = 0; j < h; j++)
                for (int i = 0; i < w; i++)
                    g->vram[((dy + j) & 511) * PSX_VRAM_W + ((dx + i) & 1023)] =
                        g->vram[((sy + j) & 511) * PSX_VRAM_W + ((sx + i) & 1023)];
        } else if (op >= 0xA0 && op < 0xC0) {   /* CPU -> VRAM: siguen los datos */
            g->xfer_x = c[1] & 1023; g->xfer_y = (c[1] >> 16) & 511;
            g->xfer_w = ((c[2] - 1) & 1023) + 1; g->xfer_h = (((c[2] >> 16) - 1) & 511) + 1;
            stale_write(g->xfer_x, g->xfer_y, g->xfer_x + g->xfer_w - 1, g->xfer_y + g->xfer_h - 1);
            if (g_rec) {   /* carga de la CPU a la VRAM: zona sucia (sube con la siguiente primitiva) */
                rec_dirty(g->xfer_x, g->xfer_y, g->xfer_x + g->xfer_w - 1, g->xfer_y + g->xfer_h - 1);
                if (!rec_off(g->xfer_x, g->xfer_y, g->xfer_x + g->xfer_w - 1, g->xfer_y + g->xfer_h - 1)) g_rec->blit = 1;
            }
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

/* etapa 8, paso 10: si alguna primitiva con textura (poligono o sprite) lee de alguno de los rectangulos
 * (inclusivos, de la VRAM): la imagen usada como textura. La estela del efecto del virus (mision 19) mezcla
 * cada imagen con la anterior asi; una intermedia no tiene esa imagen anterior y salian texturas pegadas
 * y el cielo temblando. tpage: el estado E1 de la GPU al empezar la lista (para los sprites) */
static int tex_hits(int tpage, int u1, int u2, int v1, int v2, const int (*r)[4], int nr) {
    int depth = (tpage >> 7) & 3, sh = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    int x0 = (tpage & 15) * 64, y0 = ((tpage >> 4) & 1) * 256;
    int x1 = x0 + (u1 >> sh), x2 = x0 + (u2 >> sh), y1 = y0 + v1, y2 = y0 + v2;
    for (int k = 0; k < nr; k++)
        if (x2 >= r[k][0] && x1 <= r[k][2] && y2 >= r[k][1] && y1 <= r[k][3]) return 1;
    return 0;
}
int psx_gpu_list_textures_from(uint32_t addr, const uint8_t *ram, const uint8_t *scratch, int tpage, const int (*rects)[4], int nrects) {
    int guard = 0, need = 0, len = 0, poly = 0;
    long xfer = 0;
    uint32_t cmd[16];
    while ((addr & 0xFFFFFF) != 0xFFFFFF && ++guard <= 200000) {
        const uint32_t *p = (addr & 0xFFFFFC00u) == PSX_SCRATCH_ADDR ? (const uint32_t *)(scratch + (addr & 0x3FC))
                                                                    : (const uint32_t *)(ram + (addr & (PSX_RAM_SIZE - 4)));
        uint32_t tag = p[0];
        int n = tag >> 24;
        for (int i = 1; i <= n; i++) {
            uint32_t w = p[i];
            if (xfer > 0) { xfer--; continue; }
            if (poly) { if ((w & 0xF000F000u) == 0x50005000u && len >= 2) poly = len = 0; else len++; continue; }
            if (len == 0) {
                need = cmd_length(w);
                if ((w >> 24) >= 0x48 && (w >> 24) < 0x60 && ((w >> 24) & 0x08)) { poly = 1; len = 1; continue; }
            }
            if (len < 16) cmd[len] = w;
            len++;
            if (len < need) continue;
            int op = cmd[0] >> 24;
            if (op >= 0x20 && op < 0x40 && (op & 0x04)) {   /* poligono con textura: la pagina va en el uv del segundo */
                int nv = (op & 0x08) ? 4 : 3, gour = (op & 0x10) != 0, j = 1, tp = tpage;
                int u1 = 255, u2 = 0, v1 = 255, v2 = 0;
                for (int k = 0; k < nv && j < 16; k++) {
                    if (gour && k > 0) j++;
                    j++;   /* vertice */
                    uint32_t uv = cmd[j++];
                    int u = uv & 0xFF, v = (uv >> 8) & 0xFF;
                    if (u < u1) u1 = u;
                    if (u > u2) u2 = u;
                    if (v < v1) v1 = v;
                    if (v > v2) v2 = v;
                    if (k == 1) tp = (int)(uv >> 16);
                }
                if (tex_hits(tp, u1, u2, v1, v2, rects, nrects)) return 1;
            } else if (op >= 0x60 && op < 0x80 && (op & 0x04)) {   /* sprite con textura: la pagina de E1 */
                int size = (op >> 3) & 3, sw = 1, shh = 1;
                if (size == 0) { sw = cmd[3] & 0x3FF; shh = (cmd[3] >> 16) & 0x1FF; }
                else if (size == 2) sw = shh = 8;
                else if (size == 3) sw = shh = 16;
                int u = cmd[2] & 0xFF, v = (cmd[2] >> 8) & 0xFF;
                int u2 = u + sw - 1, v2 = v + shh - 1;
                if (u2 > 255) u = 0, u2 = 255;
                if (v2 > 255) v = 0, v2 = 255;
                if (tex_hits(tpage, u, u2, v, v2, rects, nrects)) return 1;
            } else if (op == 0xE1) tpage = (int)(cmd[0] & 0x1FF);
            else if (op >= 0xA0 && op < 0xC0) {
                long cw = ((cmd[2] - 1) & 1023) + 1, ch = (((cmd[2] >> 16) - 1) & 511) + 1;
                xfer = (cw * ch + 1) / 2;
            }
            len = 0;
        }
        addr = 0x80000000u | (tag & 0xFFFFFF);
    }
    return 0;
}

/* etapa 8, paso 2: si la lista copia (VRAM -> VRAM) desde esa zona, sin dibujar nada. El efecto de
 * los motores al acelerar copia un trozo de la imagen que se esta dibujando y lo vuelve a dibujar
 * deformado: esa lista la tiene que dibujar entera el software */
int psx_gpu_list_copies_from(uint32_t addr, const uint8_t *ram, const uint8_t *scratch, int x1, int y1, int x2, int y2) {
    int guard = 0, need = 0, len = 0, poly = 0;
    long xfer = 0;   /* palabras de datos de una carga de la CPU */
    uint32_t cmd[16];
    while ((addr & 0xFFFFFF) != 0xFFFFFF && ++guard <= 200000) {
        const uint32_t *p = (addr & 0xFFFFFC00u) == PSX_SCRATCH_ADDR ? (const uint32_t *)(scratch + (addr & 0x3FC))
                                                                    : (const uint32_t *)(ram + (addr & (PSX_RAM_SIZE - 4)));
        uint32_t tag = p[0];
        int n = tag >> 24;
        for (int i = 1; i <= n; i++) {
            uint32_t w = p[i];
            if (xfer > 0) { xfer--; continue; }
            if (poly) { if ((w & 0xF000F000u) == 0x50005000u && len >= 2) poly = len = 0; else len++; continue; }
            if (len == 0) {
                need = cmd_length(w);
                if ((w >> 24) >= 0x48 && (w >> 24) < 0x60 && ((w >> 24) & 0x08)) { poly = 1; len = 1; continue; }
            }
            if (len < 16) cmd[len] = w;
            len++;
            if (len < need) continue;
            int op = cmd[0] >> 24;
            if (op >= 0x80 && op < 0xA0) {
                int sx = cmd[1] & 1023, sy = (cmd[1] >> 16) & 511;
                int cw = ((cmd[3] - 1) & 1023) + 1, ch = (((cmd[3] >> 16) - 1) & 511) + 1;
                if (sx + cw - 1 >= x1 && sx <= x2 && sy + ch - 1 >= y1 && sy <= y2) return 1;
            } else if (op >= 0xA0 && op < 0xC0) {
                long cw = ((cmd[2] - 1) & 1023) + 1, ch = (((cmd[2] >> 16) - 1) & 511) + 1;
                xfer = (cw * ch + 1) / 2;
            }
            len = 0;
        }
        addr = 0x80000000u | (tag & 0xFFFFFF);
    }
    return 0;
}

void psx_gpu_draw_list(PsxGpu *g, uint32_t addr) {
    int x1 = g->area_x1, y1 = g->area_y1, x2 = g->area_x2, y2 = g->area_y2;
    psx_gpu_draw_list_mem(g, addr, psx_ram, psx_scratch);
    /* la imagen (el area al empezar): al dia en el software, o solo en la GPU si se anoto con only */
    psx_gpu_main_mark(x1, y1, x2, y2, g_rec && g_rec_only);
}

void psx_gpu_draw_list_mem(PsxGpu *g, uint32_t addr, const uint8_t *ram, const uint8_t *scratch) {
    /* una lista corrupta (o ciclica) colgaria la consola; aqui se corta (un fotograma real no
     * llega a 10.000 paquetes) */
    int guard = 0;
    while ((addr & 0xFFFFFF) != 0xFFFFFF) {
        if (++guard > 200000) {
            static int warned;
            if (!warned++) fprintf(stderr, "gpu: lista de dibujo de mas de 200.000 paquetes: se corta\n");
            break;
        }
        const uint32_t *p = (addr & 0xFFFFFC00u) == PSX_SCRATCH_ADDR ? (const uint32_t *)(scratch + (addr & 0x3FC))
                                                                    : (const uint32_t *)(ram + (addr & (PSX_RAM_SIZE - 4)));
        uint32_t tag = p[0];
        int n = tag >> 24;
        for (int i = 1; i <= n; i++) psx_gpu_gp0(g, p[i]);
        addr = 0x80000000u | (tag & 0xFFFFFF);
    }
}

void psx_gpu_load_image(PsxGpu *g, int x, int y, int w, int h, const uint16_t *src) {
    stale_write(x, y, x + w - 1, y + h - 1);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) g->vram[((y + j) & 511) * PSX_VRAM_W + ((x + i) & 1023)] = src[j * w + i];
}

void psx_gpu_store_image(PsxGpu *g, int x, int y, int w, int h, uint16_t *dst) {
    if (g_st_n && stale_hit(x, y, x + w - 1, y + h - 1)) psx_gpu_stale_ev[2]++;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) dst[j * w + i] = g->vram[((y + j) & 511) * PSX_VRAM_W + ((x + i) & 1023)];
}
