/* libapi (BIOS), printf de libc y libsn (ver psx_libapi.h). */
#include <stdio.h>
#include <string.h>

#include "psx_libapi.h"
#include "psx_libetc.h"
#include "psx_mem.h"

static struct { int running; uint32_t base; uint16_t target; int32_t mode; } g_rc[4];
static uint32_t g_heap, g_heap_size;
static void (*g_tty)(const char *);

static int rc_index(uint32_t spec) { return (spec & 0xFFFFFFF0u) == 0xF2000000u && (spec & 15) < 4 ? (int)(spec & 15) : -1; }
static uint32_t lines_now(void) { return (uint32_t)VSync(-1) * (psx_refresh() == 50 ? 313u : 263u); }

int32_t SetRCnt(uint32_t spec, uint16_t target, int32_t mode) {
    int i = rc_index(spec);
    if (i < 0) return 0;
    g_rc[i].target = target; g_rc[i].mode = mode; g_rc[i].base = lines_now();
    return 1;
}
int32_t GetRCnt(uint32_t spec) {
    int i = rc_index(spec);
    if (i != 1) return 0;   /* solo el contador de lineas tiene modelo */
    uint32_t v = lines_now() - g_rc[1].base;
    if (g_rc[1].target) v %= (uint32_t)g_rc[1].target + 1;   /* vuelve a 0 al llegar al objetivo */
    return (int32_t)(v & 0xFFFF);
}
int32_t StartRCnt(uint32_t spec) { int i = rc_index(spec); if (i < 0) return 0; g_rc[i].running = 1; return 1; }
int32_t StopRCnt(uint32_t spec) { int i = rc_index(spec); if (i < 0) return 0; g_rc[i].running = 0; return 1; }
int32_t ResetRCnt(uint32_t spec) { int i = rc_index(spec); if (i < 0) return 0; g_rc[i].base = lines_now(); return 1; }
void InitHeap(uint32_t *head, uint32_t size) { g_heap = psx_addr(head); g_heap_size = size; }
void EnterCriticalSection(void) {}
void ExitCriticalSection(void) {}

void psx_set_tty(void (*fn)(const char *text)) { g_tty = fn; }

static void put(char *out, int max, int *n, char c) {
    if (*n < max - 1) out[*n] = c;
    (*n)++;
}

static int format(char *out, int max, const char *fmt, const uint32_t *args, int sp) {
    int n = 0, ai = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { put(out, max, &n, *p); continue; }
        p++;
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0, width = 0, prec = -1;
        for (;; p++) {
            if (*p == '-') left = 1;
            else if (*p == '+') plus = 1;
            else if (*p == ' ') space = 1;
            else if (*p == '#') alt = 1;
            else if (*p == '0') zero = 1;
            else break;
        }
        if (*p == '*') { width = (int32_t)args[ai++]; p++; if (width < 0) { left = 1; width = -width; } }
        else while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            p++; prec = 0;
            if (*p == '*') { prec = (int32_t)args[ai++]; p++; }
            else while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
        }
        int half = 0;
        if (*p == 'h') { half = 1; p++; } else if (*p == 'l' || *p == 'L') p++;
        char c = *p, num[40];
        int len = 0, neg = 0;
        const char *str = num, *pre = "";
        if (!c) break;
        switch (c) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            uint32_t v = args[ai++];
            /* printf de PsyQ: h extiende el signo desde 16 bits en todas menos %p;
             * sprintf: como C (corto con signo en d/i, sin signo en el resto) */
            if (half && (c != 'p' || sp)) v = (!sp || c == 'd' || c == 'i') ? (uint32_t)(int32_t)(int16_t)v : (v & 0xFFFF);
            if ((c == 'd' || c == 'i') && (int32_t)v < 0) { neg = 1; v = (uint32_t)-(int32_t)v; }
            int base = (c == 'x' || c == 'X' || c == 'p') ? 16 : c == 'o' ? 8 : 10;
            const char *dg = (c == 'X' || (sp && c == 'p')) ? "0123456789ABCDEF" : "0123456789abcdef";
            if (sp && c == 'p') prec = 8;   /* sprintf: %p = 8 cifras en mayusculas */
            char tmp[40];
            int t = 0;
            do { tmp[t++] = dg[v % (uint32_t)base]; v /= (uint32_t)base; } while (v);
            if (prec == 0 && t == 1 && tmp[0] == '0') t = 0;
            if (neg) pre = "-"; else if ((c == 'd' || c == 'i') && plus) pre = "+";
            else if ((c == 'd' || c == 'i') && space) pre = " ";
            else if (alt && c == 'x' && (t || sp)) pre = "0x";   /* sprintf: prefijo aunque no haya cifras */
            else if (alt && c == 'X' && (t || sp)) pre = "0X";
            else if (alt && c == 'p' && sp) pre = "0p";            /* sprintf: "0" + la conversion */
            else if (alt && c == 'o' && (t == 0 || tmp[t - 1] != '0')) pre = "0";
            /* printf de PsyQ: la precision cuenta tambien el signo o el prefijo; sprintf: no */
            while (t + (sp ? 0 : (int)strlen(pre)) < prec) tmp[t++] = '0';
            /* sprintf: el 0 del octal solo si la primera cifra (ya con la precision) no lo es */
            if (sp && alt && c == 'o') pre = (t == 0 || tmp[t - 1] != '0') ? "0" : "";
            for (int k = 0; k < t; k++) num[k] = tmp[t - 1 - k];
            len = t;
            break;
        }
        case 'c': num[0] = (char)args[ai++]; len = 1; break;
        case 's': {
            uint32_t a = args[ai++];
            str = a ? (const char *)psx_ptr(a) : "(null)";
            if (sp && alt) {   /* sprintf: %#s = cadena Pascal (longitud en el primer byte) */
                len = (uint8_t)*str;
                str++;
            } else {
                len = (int)strlen(str);
            }
            if (prec >= 0 && len > prec) len = prec;
            break;
        }
        default:   /* '%%' y desconocidos: el caracter, sin relleno (como PsyQ) */
            put(out, max, &n, c);
            continue;
        }
        int plen = (int)strlen(pre), pad = width - len - plen;
        /* PsyQ: el 0 rellena tambien %c y %s; en los numeros, solo sin precision */
        int zpad = zero && !left && (sp ? (prec < 0 && c != 's' && c != 'c') : (prec < 0 || c == 's' || c == 'c'));
        if (!left && !zpad) while (pad-- > 0) put(out, max, &n, ' ');
        for (int k = 0; k < plen; k++) put(out, max, &n, pre[k]);
        if (zpad) while (pad-- > 0) put(out, max, &n, '0');
        for (int k = 0; k < len; k++) put(out, max, &n, str[k]);
        if (left) while (pad-- > 0) put(out, max, &n, ' ');
    }
    if (max > 0) out[n < max ? n : max - 1] = 0;
    return n;
}

int psx_format(char *out, int max, const char *fmt, const uint32_t *args) { return format(out, max, fmt, args, 0); }
int psx_sformat(char *out, int max, const char *fmt, const uint32_t *args) { return format(out, max, fmt, args, 1); }

int psx_printf(const char *fmt, const uint32_t *args) {
    char buf[1024];
    int n = psx_format(buf, sizeof buf, fmt, args);
    if (g_tty) g_tty(buf); else fputs(buf, stdout);
    return n;
}

int psx_puts(const char *s) {
    if (g_tty) { g_tty(s); g_tty("\n"); } else { fputs(s, stdout); fputc('\n', stdout); }
    return 0;
}

int64_t psx_divdi3(int64_t a, int64_t b) {
    if (!b) return 0;
    if (b == -1) return (int64_t)(0 - (uint64_t)a);   /* INT64_MIN / -1 es indefinido en C: da la vuelta */
    return a / b;
}
uint64_t psx_udivdi3(uint64_t a, uint64_t b) { return b ? a / b : 0; }

/* ---- eventos de la BIOS (OpenEvent...): modelo de banderas; DeliverEvent marca como
 * pendientes los eventos abiertos y activos de esa clase y especificacion, y TestEvent
 * devuelve 1 una vez por entrega. WaitEvent no puede bloquear: devuelve 1. ---- */
#define MAX_EV 32
static struct { int used, enabled, pending; uint32_t cls, spec; int32_t mode; uint32_t func; } g_ev[MAX_EV];

static int ev_index(uint32_t h) { uint32_t i = h - 0xF1000000u; return i < MAX_EV && g_ev[i].used ? (int)i : -1; }

uint32_t OpenEvent(uint32_t cls, uint32_t spec, int32_t mode, uint32_t func) {
    for (int i = 0; i < MAX_EV; i++)
        if (!g_ev[i].used) {
            g_ev[i].used = 1; g_ev[i].enabled = 0; g_ev[i].pending = 0;
            g_ev[i].cls = cls; g_ev[i].spec = spec; g_ev[i].mode = mode; g_ev[i].func = func;
            return 0xF1000000u + (uint32_t)i;
        }
    return 0xFFFFFFFFu;
}
int32_t CloseEvent(uint32_t h) { int i = ev_index(h); if (i < 0) return 0; g_ev[i].used = 0; return 1; }
int32_t EnableEvent(uint32_t h) { int i = ev_index(h); if (i < 0) return 0; g_ev[i].enabled = 1; return 1; }
int32_t DisableEvent(uint32_t h) { int i = ev_index(h); if (i < 0) return 0; g_ev[i].enabled = 0; return 1; }
int32_t TestEvent(uint32_t h) {
    int i = ev_index(h);
    if (i < 0 || !g_ev[i].pending) return 0;
    g_ev[i].pending = 0;
    return 1;
}
int32_t WaitEvent(uint32_t h) { int i = ev_index(h); if (i < 0) return 0; g_ev[i].pending = 0; return 1; }
void DeliverEvent(uint32_t cls, uint32_t spec) {
    for (int i = 0; i < MAX_EV; i++)
        if (g_ev[i].used && g_ev[i].cls == cls && g_ev[i].spec == spec) {
            g_ev[i].pending = 1;
            if (g_ev[i].func && g_ev[i].mode == 0x1000) psx_call(g_ev[i].func, 0, 0);   /* EvMdINTR */
        }
}
