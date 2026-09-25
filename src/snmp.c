/*
 * Minimal SNMPv2c client — see snmp.h for why it exists and what it is not.
 *
 * Encoding is only ever done for our own GETBULK request (fixed shape, small
 * non-negative integers). Decoding handles whatever arrives on the socket, so
 * it is written defensively: a TLV reader that refuses any length reaching past
 * its buffer, OIDs capped in arc count and arc size, and a walk that must move
 * strictly forward (a buggy agent cannot make it loop).
 */
#include "snmp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SNMP_PORT        161
#define SNMP_TIMEOUT_MS  700    /* per attempt; the agent sits on the local VM bridge */
#define SNMP_TRIES       2
#define SNMP_BULK_REPS   24     /* varbinds asked for per GETBULK round trip */
#define SNMP_MAX_ROUNDS  64     /* hard stop, whatever the agent does */
#define SNMP_PKT_MAX     8192
#define SNMP_COMMUNITY_MAX 64

/* BER tags */
#define T_INT        0x02
#define T_OCTSTR     0x04
#define T_NULL       0x05
#define T_OID        0x06
#define T_SEQ        0x30
#define T_COUNTER32  0x41
#define T_GAUGE32    0x42
#define T_TIMETICKS  0x43
#define T_COUNTER64  0x46
#define T_ENDOFMIB   0x82
#define PDU_RESPONSE 0xA2
#define PDU_GETBULK  0xA5

/* ---------------------------------------------------------------- encoding */

typedef struct { unsigned char b[512]; size_t n; int bad; } wbuf_t;

static void w_raw(wbuf_t *w, const void *d, size_t n)
{
    if (w->bad || n > sizeof(w->b) - w->n) { w->bad = 1; return; }
    memcpy(w->b + w->n, d, n);
    w->n += n;
}

static void w_byte(wbuf_t *w, unsigned char c) { w_raw(w, &c, 1); }

static void w_cat(wbuf_t *w, const wbuf_t *c)
{
    if (c->bad) { w->bad = 1; return; }
    w_raw(w, c->b, c->n);
}

static void w_len(wbuf_t *w, size_t n)
{
    if (n < 0x80) { w_byte(w, (unsigned char)n); return; }
    if (n < 0x100) { w_byte(w, 0x81); w_byte(w, (unsigned char)n); return; }
    w_byte(w, 0x82);
    w_byte(w, (unsigned char)(n >> 8));
    w_byte(w, (unsigned char)n);
}

static void w_tlv(wbuf_t *w, unsigned char tag, const wbuf_t *c)
{
    if (c->bad) { w->bad = 1; return; }
    w_byte(w, tag);
    w_len(w, c->n);
    w_cat(w, c);
}

/* non-negative INTEGER, minimal big-endian two's complement */
static void w_uint(wbuf_t *w, unsigned long v)
{
    unsigned char tmp[9];
    int n = 0;
    do { tmp[n++] = (unsigned char)(v & 0xff); v >>= 8; } while (v);
    if (tmp[n - 1] & 0x80) tmp[n++] = 0;       /* keep the sign bit clear */
    w_byte(w, T_INT);
    w_len(w, (size_t)n);
    while (n) w_byte(w, tmp[--n]);
}

static void w_oid(wbuf_t *w, const unsigned *oid, int n)
{
    wbuf_t c = { .n = 0 };
    if (n < 2 || oid[0] > 2 || oid[1] >= 40) { w->bad = 1; return; }
    w_byte(&c, (unsigned char)(oid[0] * 40 + oid[1]));
    for (int i = 2; i < n; i++) {
        unsigned char tmp[5];
        int k = 0;
        unsigned v = oid[i];
        do { tmp[k++] = (unsigned char)(v & 0x7f); v >>= 7; } while (v);
        while (k > 1) w_byte(&c, (unsigned char)(tmp[--k] | 0x80));
        w_byte(&c, tmp[0]);
    }
    w_tlv(w, T_OID, &c);
}

static size_t build_getbulk(unsigned char *out, size_t cap, const char *community,
                            long reqid, const unsigned *oid, int oid_len)
{
    wbuf_t vb = { .n = 0 }, one = { .n = 0 }, list = { .n = 0 };
    wbuf_t body = { .n = 0 }, pdu = { .n = 0 }, m = { .n = 0 }, msg = { .n = 0 };

    w_oid(&vb, oid, oid_len);
    w_byte(&vb, T_NULL);
    w_byte(&vb, 0);
    w_tlv(&one, T_SEQ, &vb);                    /* varbind */
    w_tlv(&list, T_SEQ, &one);                  /* varbind list */

    w_uint(&body, (unsigned long)reqid);
    w_uint(&body, 0);                           /* non-repeaters */
    w_uint(&body, SNMP_BULK_REPS);              /* max-repetitions */
    w_cat(&body, &list);
    w_tlv(&pdu, PDU_GETBULK, &body);

    size_t cl = strlen(community);
    w_uint(&m, 1);                              /* version: 1 = SNMPv2c */
    w_byte(&m, T_OCTSTR);
    w_len(&m, cl);
    w_raw(&m, community, cl);
    w_cat(&m, &pdu);
    w_tlv(&msg, T_SEQ, &m);

    if (msg.bad || msg.n > cap) return 0;
    memcpy(out, msg.b, msg.n);
    return msg.n;
}

/* ---------------------------------------------------------------- decoding */

typedef struct { const unsigned char *p, *end; } rd_t;

/* Read one TLV; c gets its content. Every length is checked against what is
 * actually left in the buffer before anything is dereferenced. */
static int rd_tlv(rd_t *r, unsigned char *tag, rd_t *c)
{
    if (r->end - r->p < 2) return -1;
    unsigned char t = *r->p++;
    if ((t & 0x1f) == 0x1f) return -1;          /* multi-byte tags: never used by SNMP */
    size_t len = *r->p++;
    if (len & 0x80) {
        int n = (int)(len & 0x7f);
        if (n < 1 || n > 3 || r->end - r->p < n) return -1;
        len = 0;
        while (n--) len = (len << 8) | *r->p++;
    }
    if (len > (size_t)(r->end - r->p)) return -1;
    *tag = t;
    c->p = r->p;
    c->end = r->p + len;
    r->p += len;
    return 0;
}

static int rd_expect(rd_t *r, unsigned char want, rd_t *c)
{
    unsigned char t;
    return (rd_tlv(r, &t, c) == 0 && t == want) ? 0 : -1;
}

static int rd_num(const rd_t *c, int is_unsigned, long long *v)
{
    size_t n = (size_t)(c->end - c->p);
    if (n < 1 || n > 9) return -1;
    if (n == 9 && c->p[0] != 0) return -1;      /* 9 bytes only with a leading pad */
    unsigned long long u = 0;
    for (size_t i = 0; i < n; i++) u = (u << 8) | c->p[i];
    if (!is_unsigned && (c->p[0] & 0x80) && n < 8)
        u |= ~0ULL << (8 * n);                  /* sign-extend a negative INTEGER */
    *v = (long long)u;
    return 0;
}

static int rd_oid(const rd_t *c, unsigned *oid, int max, int *len)
{
    const unsigned char *p = c->p;
    int n = 0;
    while (p < c->end) {
        unsigned long long v = 0;
        int k = 0;
        for (;;) {
            if (p >= c->end || ++k > 5) return -1;
            unsigned char b = *p++;
            v = (v << 7) | (b & 0x7f);
            if (!(b & 0x80)) break;
        }
        if (v > 0xffffffffULL) return -1;
        if (n == 0) {                           /* first subidentifier holds two arcs */
            if (max < 2) return -1;
            unsigned a = v < 40 ? 0 : v < 80 ? 1 : 2;
            oid[0] = a;
            oid[1] = (unsigned)(v - 40ULL * a);
            n = 2;
        } else {
            if (n >= max) return -1;
            oid[n++] = (unsigned)v;
        }
    }
    *len = n;
    return n >= 2 ? 0 : -1;
}

/* -1 = malformed / agent error, -2 = well-formed but for another request id
 * (a late answer to an earlier try), else the number of varbinds stored. */
int snmp_parse_response(const unsigned char *buf, size_t len, long reqid,
                        snmp_varbind_t *out, int max)
{
    rd_t r = { buf, buf + len }, msg, x, pdu, list, vb;
    long long v;
    unsigned char t;

    if (rd_expect(&r, T_SEQ, &msg)) return -1;
    if (rd_expect(&msg, T_INT, &x) || rd_num(&x, 0, &v) || v != 1) return -1;   /* v2c */
    if (rd_expect(&msg, T_OCTSTR, &x)) return -1;                               /* community */
    if (rd_expect(&msg, PDU_RESPONSE, &pdu)) return -1;
    if (rd_expect(&pdu, T_INT, &x) || rd_num(&x, 0, &v)) return -1;
    if (v != reqid) return -2;
    if (rd_expect(&pdu, T_INT, &x) || rd_num(&x, 0, &v) || v != 0) return -1;   /* error-status */
    if (rd_expect(&pdu, T_INT, &x)) return -1;                                  /* error-index */
    if (rd_expect(&pdu, T_SEQ, &list)) return -1;

    int n = 0;
    while (list.p < list.end && n < max) {
        snmp_varbind_t *o = &out[n];
        memset(o, 0, sizeof(*o));
        if (rd_expect(&list, T_SEQ, &vb)) return -1;
        if (rd_expect(&vb, T_OID, &x) || rd_oid(&x, o->oid, SNMP_OID_MAX, &o->oid_len)) return -1;
        if (rd_tlv(&vb, &t, &x)) return -1;
        o->type = t;
        switch (t) {
        case T_INT:
            if (rd_num(&x, 0, &o->num)) return -1;
            break;
        case T_COUNTER32: case T_GAUGE32: case T_TIMETICKS: case T_COUNTER64:
            if (rd_num(&x, 1, &o->num)) return -1;
            break;
        case T_OCTSTR: {
            size_t sl = (size_t)(x.end - x.p);
            if (sl >= sizeof(o->str)) sl = sizeof(o->str) - 1;
            memcpy(o->str, x.p, sl);
            o->str[sl] = '\0';
            break;
        }
        default:            /* NULL, endOfMibView & co: the tag says it all */
            break;
        }
        n++;
    }
    return n;
}

/* ---------------------------------------------------------------- transport */

static long new_reqid(void)
{
    unsigned v = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        if (read(fd, &v, sizeof(v)) != (ssize_t)sizeof(v)) v = 0;
        close(fd);
    }
    if (!v) v = (unsigned)time(NULL) ^ ((unsigned)getpid() << 16);
    return (long)((v & 0x7fffffff) | 1);
}

static int elapsed_ms(const struct timespec *t0)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int)((t.tv_sec - t0->tv_sec) * 1000 + (t.tv_nsec - t0->tv_nsec) / 1000000);
}

static int open_peer(const char *ip, char *err, size_t errsz)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    memset(&ss, 0, sizeof(ss));
    struct sockaddr_in *s4 = (struct sockaddr_in *)&ss;
    struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&ss;
    if (inet_pton(AF_INET, ip, &s4->sin_addr) == 1) {
        s4->sin_family = AF_INET;
        s4->sin_port = htons(SNMP_PORT);
        sl = sizeof(*s4);
    } else if (inet_pton(AF_INET6, ip, &s6->sin6_addr) == 1) {
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons(SNMP_PORT);
        sl = sizeof(*s6);
    } else {
        snprintf(err, errsz, "%s is not an IP address (host names are not supported)", ip);
        return -1;
    }
    int fd = socket(ss.ss_family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        snprintf(err, errsz, "socket: %s", strerror(errno));
        return -1;
    }
    /* a connected UDP socket: the kernel drops datagrams from any other source */
    if (connect(fd, (struct sockaddr *)&ss, sl) != 0) {
        snprintf(err, errsz, "connect: %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* One GETBULK round trip (with a retry). Returns the varbind count, -1 on error. */
static int bulk(int fd, const char *community, const unsigned *oid, int oid_len,
                snmp_varbind_t *out, int max, char *err, size_t errsz)
{
    unsigned char req[600], resp[SNMP_PKT_MAX];
    long id = new_reqid();
    size_t rl = build_getbulk(req, sizeof(req), community, id, oid, oid_len);
    if (!rl) {
        snprintf(err, errsz, "cannot encode the request");
        return -1;
    }
    for (int attempt = 0; attempt < SNMP_TRIES; attempt++) {
        if (send(fd, req, rl, 0) != (ssize_t)rl) {
            snprintf(err, errsz, "send: %s", strerror(errno));
            return -1;
        }
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (;;) {
            int left = SNMP_TIMEOUT_MS - elapsed_ms(&t0);
            if (left <= 0) break;
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            int pr = poll(&pfd, 1, left);
            if (pr < 0 && errno == EINTR) continue;
            if (pr <= 0) break;
            ssize_t n = recv(fd, resp, sizeof(resp), 0);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                if (errno == ECONNREFUSED) {
                    snprintf(err, errsz, "port 161 refused — is the SNMP service running?");
                    return -1;
                }
                snprintf(err, errsz, "recv: %s", strerror(errno));
                return -1;
            }
            int got = snmp_parse_response(resp, (size_t)n, id, out, max);
            if (got == -2) continue;            /* late answer to an earlier try */
            if (got < 0) {
                snprintf(err, errsz, "malformed or error response from the agent");
                return -1;
            }
            return got;
        }
    }
    snprintf(err, errsz, "no answer — SNMP service off, wrong community, or a firewall?");
    return -1;
}

static int oid_under(const unsigned *oid, int len, const unsigned *root, int rlen)
{
    if (len <= rlen) return 0;
    for (int i = 0; i < rlen; i++)
        if (oid[i] != root[i]) return 0;
    return 1;
}

static int oid_cmp(const unsigned *a, int al, const unsigned *b, int bl)
{
    int n = al < bl ? al : bl;
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return al - bl;
}

int snmp_walk(const char *ip, const char *community,
              const unsigned *root, int root_len,
              snmp_varbind_t *out, int max, char *err, size_t errsz)
{
    if (!ip || !*ip || root_len < 2 || root_len > SNMP_OID_MAX) {
        snprintf(err, errsz, "bad walk parameters");
        return -1;
    }
    if (!community) community = "public";
    if (strlen(community) > SNMP_COMMUNITY_MAX) {
        snprintf(err, errsz, "community longer than %d characters", SNMP_COMMUNITY_MAX);
        return -1;
    }
    int fd = open_peer(ip, err, errsz);
    if (fd < 0) return -1;

    unsigned cur[SNMP_OID_MAX];
    int cur_len = root_len;
    memcpy(cur, root, sizeof(unsigned) * (size_t)root_len);

    snmp_varbind_t batch[SNMP_BULK_REPS];
    int n = 0;
    for (int round = 0; round < SNMP_MAX_ROUNDS && n < max; round++) {
        int got = bulk(fd, community, cur, cur_len, batch, SNMP_BULK_REPS, err, errsz);
        if (got < 0) { close(fd); return -1; }
        if (got == 0) break;
        for (int i = 0; i < got; i++) {
            const snmp_varbind_t *b = &batch[i];
            if (b->type == T_ENDOFMIB || !oid_under(b->oid, b->oid_len, root, root_len))
                goto done;                      /* walked off the end of the table */
            if (oid_cmp(b->oid, b->oid_len, cur, cur_len) <= 0) {
                snprintf(err, errsz, "agent returned OIDs out of order");
                close(fd);
                return -1;
            }
            memcpy(cur, b->oid, sizeof(unsigned) * (size_t)b->oid_len);
            cur_len = b->oid_len;
            if (n < max) out[n++] = *b;
        }
    }
done:
    close(fd);
    return n;
}

/* ---------------------------------------------------------------- TrueNAS */

/* TRUENAS-MIB::hddTempEntry = enterprises.50536.3.1 — column 2 hddTempDevice
 * (e.g. "sda"), column 3 hddTempValue in milli-°C. The agent fills it from the
 * readings TrueNAS takes every 5 minutes for its own graphs, so walking it
 * costs the disks nothing. */
static const unsigned HDD_TEMP_ENTRY[] = { 1, 3, 6, 1, 4, 1, 50536, 3, 1 };
#define HDD_TEMP_ENTRY_LEN ((int)(sizeof(HDD_TEMP_ENTRY) / sizeof(HDD_TEMP_ENTRY[0])))
#define HDD_ROWS_MAX 32

/* Accept only plain device names; "vm:" plus the name must fit a 15-char drive name. */
static int name_ok(const char *s)
{
    size_t n = strlen(s);
    if (n < 1 || n > 12) return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-' && *s != '.') return 0;
    return 1;
}

int snmp_truenas_disk_temps(const char *ip, const char *community,
                            char *text, size_t textsz, char *err, size_t errsz)
{
    snmp_varbind_t vb[HDD_ROWS_MAX * 2 + 8];
    int n = snmp_walk(ip, community, HDD_TEMP_ENTRY, HDD_TEMP_ENTRY_LEN,
                      vb, (int)(sizeof(vb) / sizeof(vb[0])), err, errsz);
    if (n < 0) return -1;

    struct { unsigned idx; char name[16]; long long mc; int has_val; } row[HDD_ROWS_MAX];
    int rows = 0;
    for (int i = 0; i < n; i++) {
        if (vb[i].oid_len != HDD_TEMP_ENTRY_LEN + 2) continue;    /* entry.column.index */
        unsigned col = vb[i].oid[HDD_TEMP_ENTRY_LEN];
        unsigned idx = vb[i].oid[HDD_TEMP_ENTRY_LEN + 1];
        if (col != 2 && col != 3) continue;

        int r = 0;
        while (r < rows && row[r].idx != idx) r++;
        if (r == rows) {
            if (rows == HDD_ROWS_MAX) continue;
            memset(&row[r], 0, sizeof(row[r]));
            row[r].idx = idx;
            rows++;
        }
        if (col == 2 && vb[i].type == T_OCTSTR && name_ok(vb[i].str))
            snprintf(row[r].name, sizeof(row[r].name), "%.12s", vb[i].str);   /* name_ok: <= 12 */
        else if (col == 3 && (vb[i].type == T_GAUGE32 || vb[i].type == T_INT ||
                              vb[i].type == T_COUNTER32)) {
            row[r].mc = vb[i].num;
            row[r].has_val = 1;
        }
    }

    size_t off = 0;
    int drives = 0;
    text[0] = '\0';
    for (int r = 0; r < rows; r++) {
        if (!row[r].has_val) continue;
        char name[16], val[24];
        if (row[r].name[0])
            snprintf(name, sizeof(name), "%s", row[r].name);
        else                                    /* never drop a reading over a name */
            snprintf(name, sizeof(name), "disk%u", row[r].idx % 1000u);
        /* 0 = TrueNAS got no reading for that disk (asleep, or a virtual disk
         * without a sensor): no cooling demand rather than a dead sensor */
        if (row[r].mc > 0)
            snprintf(val, sizeof(val), "%lld", (row[r].mc + 500) / 1000);
        else
            snprintf(val, sizeof(val), "*");
        int w = snprintf(text + off, textsz - off, "vm:%s=%s\n", name, val);
        if (w < 0 || (size_t)w >= textsz - off) break;
        off += (size_t)w;
        drives++;
    }
    return drives;
}
