/* Etapa 10, pasos 7-8: el juego en red por lockstep (ver ac3_net.h). */
#include "ac3_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "psx_libc.h"
#include "psx_mem.h"
#ifdef __ANDROID__   /* en el APK, los mensajes al registro del sistema (logcat, como game_main.c) */
#include <android/log.h>
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "AC3Game", __VA_ARGS__)
#endif

#define NET_MAGIC 0x4E334341u   /* "AC3N" */
#define BEACON_MAGIC 0x4C334341u   /* "AC3L": el anuncio del anfitrion por UDP (paso 8) */
#define NET_VERSION 2
#define RING 512                /* VBlank de mandos y huellas guardados (mas que cualquier retraso) */
#define HASH_EVERY 60
#define LOST_S 20.0             /* sin nada del otro tanto tiempo: la red se da por perdida */

typedef struct { uint32_t magic, version, host, delay; NetInfo info; } Hello;
/* (paso 8) el anuncio: cada segundo, mientras el anfitrion espera, a la difusion de cada red (y a 255.255.255.255) */
typedef struct { uint32_t magic, version, port; NetInfo info; } Beacon;
/* los mensajes: 16 bytes. tipo 1, el mando del VBlank v; tipo 2, la huella de v; (paso 8) tipo 3, un ping (la hora de
 * quien lo envia, en us); tipo 4, el mismo devuelto */
typedef struct { uint8_t type, pad[3]; uint32_t v; uint8_t data[8]; } Msg;

static int g_fd = -1, g_host, g_delay, g_lost;
static char g_err[160];
static NetPad g_mine[RING], g_theirs[RING];
static int g_mine_v[RING], g_theirs_v[RING];
static uint64_t g_hmine[RING], g_htheirs[RING];
static int g_hmine_v[RING], g_htheirs_v[RING];
static int g_compared, g_differ, g_first = -1, g_waits;
static double g_wait_ms;
static double g_ping[8];
static int g_ping_n;
#define PING_EVERY 30
static uint8_t g_rx[4096];
static size_t g_rxn;

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static int send_all(const void *p, size_t n) {
    const uint8_t *b = p;
    while (n) {
        ssize_t k = send(g_fd, b, n, MSG_NOSIGNAL);
        if (k < 0 && (errno == EINTR || errno == EAGAIN)) { struct pollfd q = {g_fd, POLLOUT, 0}; poll(&q, 1, 100); continue; }
        if (k <= 0) return -1;
        b += k; n -= (size_t)k;
    }
    return 0;
}
static int recv_all(void *p, size_t n, double until) {
    uint8_t *b = p;
    while (n) {
        struct pollfd q = {g_fd, POLLIN, 0};
        if (poll(&q, 1, 200) <= 0) { if (now_s() > until) return -1; continue; }
        ssize_t k = recv(g_fd, b, n, 0);
        if (k <= 0) return -1;
        b += k; n -= (size_t)k;
    }
    return 0;
}

static void compare_hash(int v) {
    int i = v % RING;
    if (g_hmine_v[i] != v || g_htheirs_v[i] != v) return;
    g_compared++;
    if (g_hmine[i] != g_htheirs[i]) {
        if (g_differ++ == 0) g_first = v;
        if (g_differ <= 5) printf("red: las partidas se han separado: la huella del VBlank %d es distinta\n", v);
    }
    g_hmine_v[i] = g_htheirs_v[i] = -1;
}

/* lo que haya llegado, sin esperar; -1 si la conexion se cerro */
static int pump(void) {
    for (;;) {
        ssize_t k = recv(g_fd, g_rx + g_rxn, sizeof g_rx - g_rxn, MSG_DONTWAIT);
        if (k == 0) return -1;
        if (k < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break; return -1; }
        g_rxn += (size_t)k;
        size_t o = 0;
        while (g_rxn - o >= sizeof(Msg)) {
            Msg m;
            memcpy(&m, g_rx + o, sizeof m);
            o += sizeof m;
            int i = (int)(m.v % RING);
            if (m.type == 1) {
                memcpy(&g_theirs[i].buttons, m.data, 2);
                g_theirs[i].lx = m.data[2]; g_theirs[i].ly = m.data[3]; g_theirs[i].rx = m.data[4]; g_theirs[i].ry = m.data[5];
                g_theirs_v[i] = (int)m.v;
            } else if (m.type == 2) {
                memcpy(&g_htheirs[i], m.data, 8);
                g_htheirs_v[i] = (int)m.v;
                compare_hash((int)m.v);
            } else if (m.type == 3) {   /* (paso 8) el ping del otro: de vuelta, igual */
                m.type = 4;
                send_all(&m, sizeof m);
            } else if (m.type == 4) {   /* el nuestro, de vuelta */
                uint64_t t0;
                memcpy(&t0, m.data, 8);
                g_ping[g_ping_n++ % 8] = (now_s() * 1e6 - (double)t0) / 1000.0;
            }
        }
        memmove(g_rx, g_rx + o, g_rxn - o);
        g_rxn -= o;
    }
    return 0;
}

uint32_t ac3_net_disc_id(const uint8_t *ace, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) h = (h ^ ace[i]) * 0x01000193u;
    return h;
}

void ac3_net_local_ips(char *out, size_t n) {
    struct ifaddrs *ifs, *i;
    out[0] = 0;
    if (getifaddrs(&ifs)) return;
    for (i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP)) continue;
        char a[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, a, sizeof a);
        size_t l = strlen(out);
        snprintf(out + l, n - l, "%s%s", l ? ", " : "", a);
    }
    freeifaddrs(ifs);
}

const char *ac3_net_error(void) { return g_err; }

int ac3_net_send_blob(const void *p, size_t n) { return g_fd < 0 ? -1 : send_all(p, n); }
int ac3_net_recv_blob(void *p, size_t n, int timeout_s) {
    if (g_fd < 0 || recv_all(p, n, now_s() + timeout_s)) {
        snprintf(g_err, sizeof g_err, "no llego la tarjeta de memoria del otro");
        return -1;
    }
    return 0;
}

/* el anuncio, a la difusion de cada red con difusion y a la general (en una zona Wi-Fi, la del aparato que la da) */
static void beacon(int us, int port, const NetInfo *mine) {
    Beacon b = {BEACON_MAGIC, NET_VERSION, (uint32_t)port, *mine};
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = htonl(INADDR_BROADCAST)};
    sendto(us, &b, sizeof b, 0, (struct sockaddr *)&to, sizeof to);
    struct ifaddrs *ifs, *i;
    if (getifaddrs(&ifs)) return;
    for (i = ifs; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_BROADCAST) && (i->ifa_flags & IFF_UP) &&
            !(i->ifa_flags & IFF_LOOPBACK) && i->ifa_broadaddr) {
            to.sin_addr = ((struct sockaddr_in *)i->ifa_broadaddr)->sin_addr;
            sendto(us, &b, sizeof b, 0, (struct sockaddr *)&to, sizeof to);
        }
    freeifaddrs(ifs);
}

/* el saludo por g_fd: 0 si el otro es compatible; si no llega, them->magic queda a 0 */
static int hello(int host, int delay, const NetInfo *mine, Hello *them, double wait_s) {
    Hello me = {NET_MAGIC, NET_VERSION, (uint32_t)host, (uint32_t)delay, *mine};
    int one = 1;
    setsockopt(g_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    memset(them, 0, sizeof *them);
    if (send_all(&me, sizeof me) || recv_all(them, sizeof *them, now_s() + wait_s)) { them->magic = 0; return -1; }
    if (them->magic != NET_MAGIC || them->version != NET_VERSION || them->host == (uint32_t)host) {
        snprintf(g_err, sizeof g_err, "el otro no es un AC3 Recomp compatible (version del protocolo %u, aqui %u)", them->version, NET_VERSION);
        return 1;
    }
    if (them->info.disc != mine->disc) {
        snprintf(g_err, sizeof g_err, "el otro tiene otro disco (ACE.BIN %08x, aqui %08x)", them->info.disc, mine->disc);
        return 1;
    }
    return 0;
}

int ac3_net_open(int host, const char *ip, int port, int delay, const NetInfo *mine, NetInfo *theirs, int timeout_s) {
    double until = now_s() + timeout_s;
    Hello them;
    g_err[0] = 0;
    for (int i = 0; i < RING; i++) g_mine_v[i] = g_theirs_v[i] = g_hmine_v[i] = g_htheirs_v[i] = -1;
    if (host) {
        int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (ls < 0 || bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 1)) {
            printf("red: no se puede escuchar en el puerto %d (%s)\n", port, strerror(errno));
            if (ls >= 0) close(ls);
            return -1;
        }
        char ips[128];
        ac3_net_local_ips(ips, sizeof ips);
        printf("red: anfitrion, esperando al invitado en el puerto %d (%s)\n", port, ips[0] ? ips : "sin red");
        int us = socket(AF_INET, SOCK_DGRAM, 0);
        if (us >= 0) setsockopt(us, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
        double next = 0;
        while (g_fd < 0) {
            if (us >= 0 && now_s() >= next) { beacon(us, port, mine); next = now_s() + 1.0; }
            struct pollfd q = {ls, POLLIN, 0};
            if (poll(&q, 1, 250) > 0) g_fd = accept(ls, NULL, NULL);
            if (now_s() > until) break;
        }
        if (us >= 0) close(us);
        close(ls);
    } else {
        struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
        inet_pton(AF_INET, ip, &a.sin_addr);
        printf("red: invitado, conectando con %s:%d\n", ip, port);
        /* con adb reverse el cable acepta la conexion aunque el anfitrion aun no escuche, y la cierra sin saludo:
         * entonces se reintenta (solo un saludo que llega y no cuadra es un fallo) */
        while (g_fd < 0 && now_s() < until) {
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (s >= 0 && connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
                g_fd = s;
                if (!hello(host, delay, mine, &them, 10)) break;
                close(g_fd); g_fd = -1;
                if (them.magic) { printf("red: %s\n", g_err); return -1; }
            } else if (s >= 0) close(s);
            usleep(500000);
        }
    }
    if (g_fd < 0) {
        snprintf(g_err, sizeof g_err, "sin conexion en %d s", timeout_s);
        printf("red: %s\n", g_err);
        return -1;
    }
    if (host && hello(host, delay, mine, &them, 10)) {
        if (!them.magic) snprintf(g_err, sizeof g_err, "el otro no saluda");
        printf("red: %s\n", g_err);
        close(g_fd); g_fd = -1;
        return -1;
    }
    g_host = host;
    g_delay = host ? delay : (int)them.delay;   /* manda el retraso del anfitrion */
    them.info.name[sizeof them.info.name - 1] = them.info.label[sizeof them.info.label - 1] = 0;
    if (theirs) *theirs = them.info;
    if (them.info.opts != mine->opts)
        printf("red: AVISO: opciones distintas (aqui %x, el otro %x): las partidas se separaran si no se igualan (16:9 o 4:3)\n",
               mine->opts, them.info.opts);
    printf("red: conectado como %s con %s; retraso %d VBlank\n", host ? "anfitrion (jugador 1)" : "invitado (jugador 2)",
           them.info.name[0] ? them.info.name : "?", g_delay);
    return 0;
}

int ac3_net_active(void) { return g_fd >= 0 && !g_lost; }
int ac3_net_lost(void) { return g_fd >= 0 && g_lost; }
int ac3_net_is_host(void) { return g_host; }

void ac3_net_frame(int v, const NetPad *local, NetPad *p1, NetPad *p2) {
    static const NetPad idle = {0, 0x80, 0x80, 0x80, 0x80};
    *p1 = *p2 = idle;
    if (g_fd < 0) return;
    /* el mando de ahora, para v + retraso: guardado y enviado */
    int f = v + g_delay, i = f % RING;
    g_mine[i] = *local; g_mine_v[i] = f;
    if (!g_lost) {
        Msg m = {1, {0}, (uint32_t)f, {0}};
        memcpy(m.data, &local->buttons, 2);
        m.data[2] = local->lx; m.data[3] = local->ly; m.data[4] = local->rx; m.data[5] = local->ry;
        if (send_all(&m, sizeof m)) { g_lost = 1; printf("red: conexion perdida al enviar (VBlank %d)\n", v); }
    }
    if (!g_lost && v % PING_EVERY == 0) {   /* (paso 8) el ping */
        Msg p = {3, {0}, (uint32_t)v, {0}};
        uint64_t t0 = (uint64_t)(now_s() * 1e6);
        memcpy(p.data, &t0, 8);
        send_all(&p, sizeof p);
    }
    if (v < g_delay) return;   /* los primeros VBlank, sin mando en los dos */
    int j = v % RING;
    NetPad mine = g_mine_v[j] == v ? g_mine[j] : idle, theirs = idle;
    if (!g_lost) {
        double t0 = now_s(), last = t0;
        int waited = 0;
        for (;;) {
            if (pump()) { g_lost = 1; printf("red: el otro cerro la conexion (VBlank %d)\n", v); break; }
            if (g_theirs_v[j] == v) { theirs = g_theirs[j]; break; }
            waited = 1;
            struct pollfd q = {g_fd, POLLIN, 0};
            poll(&q, 1, 5);
            double t = now_s();
            if (t - last > 2.0) { printf("red: esperando el mando del otro para el VBlank %d (%.0f s)\n", v, t - t0); last = t; }
            if (t - t0 > LOST_S) { g_lost = 1; printf("red: el otro no responde: red perdida en el VBlank %d\n", v); break; }
        }
        if (waited) { g_waits++; g_wait_ms += (now_s() - t0) * 1000; }
    }
    if (g_host) { *p1 = mine; *p2 = theirs; }
    else { *p1 = theirs; *p2 = mine; }
}

int ac3_net_hash_due(int v) { return g_fd >= 0 && v > 0 && v % HASH_EVERY == 0; }
void ac3_net_hash(int v, uint64_t h) {
    if (g_fd < 0) return;
    int i = v % RING;
    g_hmine[i] = h; g_hmine_v[i] = v;
    if (!g_lost) {
        Msg m = {2, {0}, (uint32_t)v, {0}};
        memcpy(m.data, &h, 8);
        if (send_all(&m, sizeof m)) g_lost = 1;
        pump();
    }
    compare_hash(v);
}

uint64_t ac3_net_ram_hash(void) {
    uint64_t h[33], r = 0xCBF29CE484222325ull;
    psx_mem_hash(h);
    for (int i = 0; i < 33; i++) r = (r ^ h[i]) * 0x100000001B3ull;
    return (r ^ psx_rand_seed()) * 0x100000001B3ull;
}

double ac3_net_ping_ms(void) {
    int n = g_ping_n < 8 ? g_ping_n : 8;
    if (!n) return -1;
    double s = 0;
    for (int i = 0; i < n; i++) s += g_ping[i];
    return s / n;
}

void ac3_net_stats(int *compared, int *differ, int *first, int *waits, double *wait_ms) {
    *compared = g_compared; *differ = g_differ; *first = g_first; *waits = g_waits; *wait_ms = g_wait_ms;
}
