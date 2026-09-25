/* SNMP client checks (no LVGL, no hardware):
 *
 *   gcc -Iinclude -fsanitize=address,undefined -g test/snmp_check.c src/snmp.c -o snmp-check
 *   ./snmp-check                       # parser checks + fuzzing
 *   ./snmp-check 127.0.0.1 public      # plus a live walk of hddTempTable
 *
 * The parser reads datagrams from the network inside a root daemon, so beyond
 * "does it decode a good answer" the point here is that no truncated or
 * mangled answer can make it read out of bounds — run it under ASan/UBSan.
 * The responses are built by an encoder written independently from the one in
 * snmp.c, so the two cross-check each other. */
#include "snmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(const char *what, long long got, long long want)
{
    if (got == want) return;
    printf("  FAIL %-40s got %lld, want %lld\n", what, got, want);
    fails++;
}

/* ---- independent BER builder ---- */
typedef struct { unsigned char b[2048]; size_t n; } buf_t;

static void put(buf_t *o, const void *d, size_t n) { memcpy(o->b + o->n, d, n); o->n += n; }
static void put1(buf_t *o, unsigned char c) { o->b[o->n++] = c; }

static void wrap(buf_t *o, unsigned char tag, const buf_t *c)
{
    put1(o, tag);
    if (c->n < 128) put1(o, (unsigned char)c->n);
    else { put1(o, 0x82); put1(o, (unsigned char)(c->n >> 8)); put1(o, (unsigned char)c->n); }
    put(o, c->b, c->n);
}

static void integer(buf_t *o, unsigned char tag, unsigned long long v)
{
    unsigned char t[9];
    int n = 0;
    do { t[n++] = (unsigned char)v; v >>= 8; } while (v);
    if (t[n - 1] & 0x80) t[n++] = 0;
    put1(o, tag);
    put1(o, (unsigned char)n);
    while (n) put1(o, t[--n]);
}

static void octets(buf_t *o, const char *s)
{
    put1(o, 0x04);
    put1(o, (unsigned char)strlen(s));
    put(o, s, strlen(s));
}

static void oid(buf_t *o, const unsigned *a, int n)
{
    buf_t c = { .n = 0 };
    put1(&c, (unsigned char)(a[0] * 40 + a[1]));
    for (int i = 2; i < n; i++) {
        unsigned char t[5];
        int k = 0;
        unsigned v = a[i];
        do { t[k++] = v & 0x7f; v >>= 7; } while (v);
        while (k > 1) put1(&c, t[--k] | 0x80);
        put1(&c, t[0]);
    }
    wrap(o, 0x06, &c);
}

static const unsigned ENTRY[] = { 1, 3, 6, 1, 4, 1, 50536, 3, 1 };

/* one hddTempTable varbind: column 2 = name (OCTET STRING), 3 = Gauge32 m°C */
static void varbind(buf_t *list, unsigned col, unsigned idx, const char *name, unsigned mc)
{
    unsigned a[11];
    memcpy(a, ENTRY, sizeof(ENTRY));
    a[9] = col;
    a[10] = idx;
    buf_t vb = { .n = 0 }, seq = { .n = 0 };
    oid(&vb, a, 11);
    if (col == 2) octets(&vb, name);
    else integer(&vb, 0x42, mc);
    wrap(&seq, 0x30, &vb);
    put(list, seq.b, seq.n);
}

static size_t response(unsigned char *out, long reqid, int error_status, int end_of_mib)
{
    buf_t list = { .n = 0 }, vbl = { .n = 0 }, body = { .n = 0 }, pdu = { .n = 0 };
    buf_t m = { .n = 0 }, msg = { .n = 0 };
    varbind(&list, 2, 1, "sda", 0);
    varbind(&list, 2, 2, "sdb", 0);
    varbind(&list, 3, 1, NULL, 41000);
    varbind(&list, 3, 2, NULL, 0);
    if (end_of_mib) {                       /* endOfMibView: tag 0x82, empty */
        buf_t vb = { .n = 0 }, seq = { .n = 0 };
        unsigned a[] = { 1, 3, 6, 1, 4, 1, 50536, 4 };
        oid(&vb, a, 8);
        put1(&vb, 0x82);
        put1(&vb, 0);
        wrap(&seq, 0x30, &vb);
        put(&list, seq.b, seq.n);
    }
    wrap(&vbl, 0x30, &list);
    integer(&body, 0x02, (unsigned long long)reqid);
    integer(&body, 0x02, (unsigned long long)error_status);
    integer(&body, 0x02, 0);
    put(&body, vbl.b, vbl.n);
    wrap(&pdu, 0xA2, &body);
    integer(&m, 0x02, 1);
    octets(&m, "public");
    put(&m, pdu.b, pdu.n);
    wrap(&msg, 0x30, &m);
    memcpy(out, msg.b, msg.n);
    return msg.n;
}

static void check_parser(void)
{
    unsigned char good[2048], bad[2048];
    snmp_varbind_t vb[16];
    printf("snmp parser:\n");

    size_t len = response(good, 4242, 0, 1);
    int n = snmp_parse_response(good, len, 4242, vb, 16);
    expect("varbinds in a good answer", n, 5);
    if (n == 5) {
        expect("first OID length", vb[0].oid_len, 11);
        expect("first OID enterprise arc", vb[0].oid[6], 50536);
        expect("name column type", vb[0].type, 0x04);
        expect("name is sda", strcmp(vb[0].str, "sda"), 0);
        expect("value column type", vb[2].type, 0x42);
        expect("value in milli-degrees", vb[2].num, 41000);
        expect("endOfMibView tag kept", vb[4].type, 0x82);
    }
    expect("other request id is skipped", snmp_parse_response(good, len, 4243, vb, 16), -2);

    size_t elen = response(bad, 7, 5, 0);
    expect("error-status is rejected", snmp_parse_response(bad, elen, 7, vb, 16), -1);
    expect("room for 2 stores 2", snmp_parse_response(good, len, 4242, vb, 2), 2);

    /* every truncation of a good answer must fail cleanly */
    int trunc_ok = 1;
    for (size_t cut = 0; cut < len; cut++)
        if (snmp_parse_response(good, cut, 4242, vb, 16) >= 0) trunc_ok = 0;
    expect("all truncations rejected", trunc_ok, 1);

    /* random byte mutations: must never crash or read past the buffer (ASan) */
    srand(12345);
    for (int i = 0; i < 200000; i++) {
        memcpy(bad, good, len);
        int flips = 1 + rand() % 4;
        for (int f = 0; f < flips; f++) bad[rand() % len] = (unsigned char)rand();
        size_t l = len - (size_t)(rand() % 3 == 0 ? rand() % (int)len : 0);
        snmp_parse_response(bad, l, 4242, vb, 16);
    }
    printf("  %s\n", fails ? "FAILURES ABOVE" : "ok");
}

int main(int argc, char **argv)
{
    check_parser();

    if (argc >= 2) {
        char text[2048], err[160] = "";
        int n = snmp_truenas_disk_temps(argv[1], argc >= 3 ? argv[2] : "public",
                                        text, sizeof(text), err, sizeof(err));
        printf("live walk of %s: %d drive(s)%s%s\n", argv[1], n, n < 0 ? " — " : "", n < 0 ? err : "");
        if (n > 0) fputs(text, stdout);
    }
    return fails ? 1 : 0;
}
