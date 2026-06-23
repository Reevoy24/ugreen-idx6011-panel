/* ug-paneld local web API + dashboard server.
 *
 * Hand-rolled HTTP/1.1 (one request per connection, Connection: close), running
 * in its own pthread. It serves the static web frontend from disk and a small
 * JSON API. It NEVER touches LVGL: it reads a mutex-guarded stats snapshot the
 * main loop publishes, and enqueues GUI-affecting actions for the main loop to
 * run. Settings/fan/power actions are implemented in main.c (which owns the
 * state) and called from here. */
#include "api.h"
#include "backlight.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <errno.h>

#define WEB_DIR        "/usr/share/ug-paneld/web"
#define HDR_MAX        8192
#define BODY_MAX       (4 * 1024 * 1024)   /* generous cap for a wallpaper PNG */

static int server_fd = -1;
static pthread_t api_thread;
static volatile int api_running = 0;
static int current_brightness = 100;
static volatile int current_state = 1;
static char api_password[64] = "";

/* ---- published stats snapshot (written by main loop, read by handlers) ---- */
static api_snapshot_t snap;
static pthread_mutex_t snap_lock = PTHREAD_MUTEX_INITIALIZER;

void api_publish_stats(const api_snapshot_t *s) {
    pthread_mutex_lock(&snap_lock);
    snap = *s;
    pthread_mutex_unlock(&snap_lock);
}

/* ---- command queue (handlers enqueue, main loop drains) ---- */
#define CMDQ_LEN 16
static api_cmd_t cmdq[CMDQ_LEN];
static int cmd_head = 0, cmd_tail = 0, cmd_count = 0;
static pthread_mutex_t cmd_lock = PTHREAD_MUTEX_INITIALIZER;

int api_cmd_push(const api_cmd_t *c) {
    int rc = -1;
    pthread_mutex_lock(&cmd_lock);
    if (cmd_count < CMDQ_LEN) {
        cmdq[cmd_tail] = *c;
        cmd_tail = (cmd_tail + 1) % CMDQ_LEN;
        cmd_count++;
        rc = 0;
    }
    pthread_mutex_unlock(&cmd_lock);
    return rc;
}

int api_cmd_pop(api_cmd_t *out) {
    int rc = 0;
    pthread_mutex_lock(&cmd_lock);
    if (cmd_count > 0) {
        *out = cmdq[cmd_head];
        cmd_head = (cmd_head + 1) % CMDQ_LEN;
        cmd_count--;
        rc = 1;
    }
    pthread_mutex_unlock(&cmd_lock);
    return rc;
}

/* ---- tiny JSON value extractors (hand-rolled, like config.c) ---- */
static int json_get_int(const char *json, const char *key, int *value) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *f = strstr(json, search);
    if (!f) return -1;
    f = strchr(f + strlen(search), ':');
    if (!f) return -1;
    f++;
    while (*f == ' ' || *f == '\t') f++;
    if (strncmp(f, "true", 4) == 0)  { *value = 1; return 0; }
    if (strncmp(f, "false", 5) == 0) { *value = 0; return 0; }
    char *end;
    long v = strtol(f, &end, 10);
    if (end == f) return -1;
    *value = (int)v;
    return 0;
}

static int json_get_str(const char *json, const char *key, char *buf, size_t bufsz) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *f = strstr(json, search);
    if (!f) return -1;
    f = strchr(f + strlen(search), ':');
    if (!f) return -1;
    f++;
    while (*f == ' ' || *f == '\t') f++;
    if (*f != '"') return -1;
    f++;
    const char *end = strchr(f, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - f);
    if (len >= bufsz) len = bufsz - 1;
    memcpy(buf, f, len);
    buf[len] = '\0';
    return 0;
}

/* ---- JSON output builder (bounded append) ---- */
typedef struct { char *p; size_t cap; size_t len; } jbuf_t;

static void jappend(jbuf_t *b, const char *fmt, ...) {
    if (b->len >= b->cap) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        b->len += (size_t)n;
        if (b->len >= b->cap) b->len = b->cap - 1;
    }
}

static void jstr(jbuf_t *b, const char *s) {
    jappend(b, "\"");
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') jappend(b, "\\%c", c);
        else if (c < 0x20)         jappend(b, "\\u%04x", c);
        else                       jappend(b, "%c", c);
    }
    jappend(b, "\"");
}

/* "t:p,t:p,..." -> [{"t":..,"p":..},...] */
static void jcurve(jbuf_t *b, const char *s) {
    jappend(b, "[");
    int first = 1;
    while (s && *s) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        int t, p;
        if (sscanf(s, "%d:%d", &t, &p) == 2) {
            jappend(b, "%s{\"t\":%d,\"p\":%d}", first ? "" : ",", t, p);
            first = 0;
        }
        while (*s && *s != ',') s++;
    }
    jappend(b, "]");
}

/* ---- HTTP request reader ---- */
typedef struct {
    char  method[8];
    char  path[256];
    long  content_length;
    char  authorization[160];
    char *body;       /* malloc'd, content_length bytes, NUL-terminated */
    size_t body_len;
} http_req_t;

static void url_decode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else if (*s == '+') {
            *o++ = ' ';
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
}

/* find a header value (case-insensitive name match at a line start) */
static const char *hdr_find(const char *hdr, const char *name) {
    size_t nlen = strlen(name);
    const char *p = hdr;
    while (*p) {
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            p += nlen + 1;
            while (*p == ' ' || *p == '\t') p++;
            return p;
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return NULL;
}

static int recv_all(int fd, char *buf, size_t need) {
    size_t got = 0;
    while (got < need) {
        ssize_t n = read(fd, buf + got, need - got);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

/* Returns 0 on success, or an HTTP error code to respond with. */
static int read_request(int fd, http_req_t *req) {
    memset(req, 0, sizeof(*req));
    req->content_length = -1;

    char hdr[HDR_MAX + 1];
    size_t hlen = 0;
    char *sep = NULL;
    while (hlen < HDR_MAX) {
        ssize_t n = read(fd, hdr + hlen, HDR_MAX - hlen);
        if (n <= 0) return 400;
        hlen += (size_t)n;
        hdr[hlen] = '\0';
        sep = strstr(hdr, "\r\n\r\n");
        if (sep) break;
    }
    if (!sep) return 431;

    size_t header_bytes = (size_t)(sep - hdr) + 4;
    size_t carry = hlen - header_bytes;
    char *body_start = sep + 4;
    *sep = '\0'; /* terminate the header block for the searches below */

    if (sscanf(hdr, "%7s %255s", req->method, req->path) != 2) return 400;
    char *q = strchr(req->path, '?');
    if (q) *q = '\0';
    url_decode(req->path);

    const char *cl = hdr_find(hdr, "Content-Length");
    if (cl) req->content_length = strtol(cl, NULL, 10);
    const char *au = hdr_find(hdr, "Authorization");
    if (au) sscanf(au, "%159[^\r\n]", req->authorization);

    if (strcmp(req->method, "POST") == 0) {
        if (req->content_length < 0) return 411;
        if (req->content_length > BODY_MAX) return 413;
        req->body = malloc((size_t)req->content_length + 1);
        if (!req->body) return 500;
        size_t have = carry < (size_t)req->content_length ? carry : (size_t)req->content_length;
        memcpy(req->body, body_start, have);
        if (have < (size_t)req->content_length &&
            recv_all(fd, req->body + have, (size_t)req->content_length - have) != 0) {
            free(req->body);
            req->body = NULL;
            return 400;
        }
        req->body_len = (size_t)req->content_length;
        req->body[req->body_len] = '\0';
    }
    return 0;
}

/* ---- HTTP responses ---- */
static void send_raw(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
}

static const char *reason(int code) {
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 431: return "Request Header Fields Too Large";
    case 503: return "Service Unavailable";
    default:  return "Internal Server Error";
    }
}

static void send_json(int fd, int code, const char *json) {
    char hdr[256];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
        code, reason(code), strlen(json));
    send_raw(fd, hdr, (size_t)hn);
    send_raw(fd, json, strlen(json));
}

static void send_error(int fd, int code, const char *msg) {
    char body[192];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", msg ? msg : reason(code));
    send_json(fd, code, body);
}

static void send_auth_required(int fd) {
    const char *body = "{\"error\":\"authentication required\"}";
    char hdr[256];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"ug-paneld\"\r\n"
        "Content-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        strlen(body));
    send_raw(fd, hdr, (size_t)hn);
    send_raw(fd, body, strlen(body));
}

static const char *content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css"))  return "text/css";
    if (!strcmp(dot, ".js"))   return "text/javascript";
    if (!strcmp(dot, ".json")) return "application/json";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcmp(dot, ".png"))  return "image/png";
    if (!strcmp(dot, ".ico"))  return "image/x-icon";
    if (!strcmp(dot, ".woff2")) return "font/woff2";
    return "application/octet-stream";
}

static void serve_file(int fd, const char *path) {
    if (strstr(path, "..")) { send_error(fd, 404, "not found"); return; }
    const char *rel = (strcmp(path, "/") == 0) ? "/index.html" : path;
    char full[512];
    snprintf(full, sizeof(full), "%s%s", WEB_DIR, rel);

    int ffd = open(full, O_RDONLY);
    if (ffd < 0) { send_error(fd, 404, "not found"); return; }
    struct stat st;
    if (fstat(ffd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(ffd);
        send_error(fd, 404, "not found");
        return;
    }
    char hdr[320];
    int hn = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
        "Connection: close\r\nCache-Control: no-cache\r\n\r\n",
        content_type(full), (long)st.st_size);
    send_raw(fd, hdr, (size_t)hn);
    char buf[65536];
    ssize_t r;
    while ((r = read(ffd, buf, sizeof(buf))) > 0) send_raw(fd, buf, (size_t)r);
    close(ffd);
}

/* ---- auth ---- */
static int password_set(void) { return api_password[0] != '\0'; }

static int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static void b64decode(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    int val = 0, bits = 0;
    for (; *in && *in != '='; in++) {
        int v = b64val((unsigned char)*in);
        if (v < 0) continue;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o < outsz - 1) out[o++] = (char)((val >> bits) & 0xFF);
        }
    }
    out[o] = '\0';
}

static int ct_eq(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    size_t n = la > lb ? la : lb;
    unsigned char d = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < n; i++) {
        char ca = i < la ? a[i] : 0;
        char cb = i < lb ? b[i] : 0;
        d |= (unsigned char)(ca ^ cb);
    }
    return d == 0;
}

/* 1 if the request carries the correct Basic-auth password. */
static int password_ok(const http_req_t *req) {
    if (strncasecmp(req->authorization, "Basic ", 6) != 0) return 0;
    char dec[192];
    b64decode(req->authorization + 6, dec, sizeof(dec));
    const char *colon = strchr(dec, ':');
    const char *pass = colon ? colon + 1 : dec;
    return ct_eq(pass, api_password);
}

/* ---- validators ---- */
static int valid_language(const char *code) {
    for (int i = 0; i < i18n_language_count(); i++)
        if (strcmp(i18n_language_code(i), code) == 0) return 1;
    return 0;
}

static int valid_wallpaper(const char *name) {
    int ok = 0;
    pthread_mutex_lock(&snap_lock);
    for (int i = 0; i < snap.wp_count; i++)
        if (strcmp(snap.wp_opts[i], name) == 0) { ok = 1; break; }
    pthread_mutex_unlock(&snap_lock);
    return ok;
}

/* ---- handlers ---- */
static void handle_stats(int fd) {
    api_snapshot_t s;
    pthread_mutex_lock(&snap_lock);
    s = snap;
    pthread_mutex_unlock(&snap_lock);

    if (!s.valid) { send_json(fd, 200, "{\"valid\":false}"); return; }

    char out[8192];
    jbuf_t b = { out, sizeof(out), 0 };
    jappend(&b, "{\"valid\":true,\"uptime_seconds\":%llu,", (unsigned long long)s.sys.uptime_seconds);

    jappend(&b, "\"system\":{\"cpu_usage\":%.1f,\"ram_usage\":%.1f,\"ram_used_mb\":%.0f,"
                "\"ram_total_mb\":%.0f,\"disk_usage\":%.1f,\"disk_used_gb\":%.1f,\"disk_total_gb\":%.1f,",
            s.sys.cpu_usage, s.sys.ram_usage, s.sys.ram_used_mb, s.sys.ram_total_mb,
            s.sys.disk_usage, s.sys.disk_used_gb, s.sys.disk_total_gb);
    if (s.sys.temp_c > 0) jappend(&b, "\"temp_c\":%.1f},", s.sys.temp_c);
    else                  jappend(&b, "\"temp_c\":null},");

    if (s.has_gpu && s.gpu_usage >= 0) jappend(&b, "\"gpu\":{\"available\":true,\"usage\":%.1f},", s.gpu_usage);
    else                               jappend(&b, "\"gpu\":{\"available\":false},");

    jappend(&b, "\"net\":{\"total_rx_bps\":%.0f,\"total_tx_bps\":%.0f,\"ifaces\":[",
            s.net.total_rx_bps, s.net.total_tx_bps);
    for (int i = 0; i < s.net.iface_count; i++) {
        net_iface_t *f = &s.net.ifaces[i];
        jappend(&b, "%s{\"name\":", i ? "," : ""); jstr(&b, f->name);
        jappend(&b, ",\"link_up\":%s,\"ipv4\":", f->link_up ? "true" : "false"); jstr(&b, f->ipv4);
        jappend(&b, ",\"ipv6\":"); jstr(&b, f->ipv6);
        jappend(&b, ",\"rx_bps\":%.0f,\"tx_bps\":%.0f}", f->rx_bps, f->tx_bps);
    }
    jappend(&b, "]},");

    jappend(&b, "\"disks\":{\"count\":%d,\"items\":[", s.disks.count);
    for (int i = 0; i < s.disks.count; i++) {
        disk_info_t *d = &s.disks.disks[i];
        jappend(&b, "%s{\"dev\":", i ? "," : ""); jstr(&b, d->dev);
        jappend(&b, ",\"is_nvme\":%s,\"idx\":%d,\"size_tb\":%.1f,",
                d->is_nvme ? "true" : "false", d->idx, d->size_tb);
        if (d->temp_c >= 0) jappend(&b, "\"temp_c\":%.0f,", d->temp_c);
        else                jappend(&b, "\"temp_c\":null,");
        jappend(&b, "\"online\":%s}", d->online ? "true" : "false");
    }
    jappend(&b, "]},");

    if (s.has_pve) {
        jappend(&b, "\"pve\":{\"available\":true,\"vm_total\":%d,\"vm_running\":%d,"
                    "\"lxc_total\":%d,\"lxc_running\":%d,\"guests\":[",
                s.pve.vm_total, s.pve.vm_running, s.pve.lxc_total, s.pve.lxc_running);
        for (int i = 0; i < s.pve.count; i++) {
            pve_guest_t *g = &s.pve.guests[i];
            jappend(&b, "%s{\"name\":", i ? "," : ""); jstr(&b, g->name);
            jappend(&b, ",\"vmid\":%d,\"running\":%s,\"is_lxc\":%s}",
                    g->vmid, g->running ? "true" : "false", g->is_lxc ? "true" : "false");
        }
        jappend(&b, "]},");
    } else {
        jappend(&b, "\"pve\":{\"available\":false},");
    }

    if (s.has_opnsense) {
        jappend(&b, "\"opnsense\":{\"available\":true,\"wan_in_bps\":%.0f,\"wan_out_bps\":%.0f,"
                    "\"gw_rtt_ms\":%d,\"gw_status\":",
                s.opn.wan_in_bps, s.opn.wan_out_bps, s.opn.gw_rtt_ms);
        jstr(&b, s.opn.gw_status);
        jappend(&b, ",\"update_status\":"); jstr(&b, s.opn.update_status);
        jappend(&b, ",\"dhcp_leases\":%d,\"dns_queries\":%d,\"dns_blocked\":%d,\"dns_blocked_pct\":%d},",
                s.opn.dhcp_leases, s.opn.dns_queries, s.opn.dns_blocked, s.opn.dns_blocked_pct);
    } else {
        jappend(&b, "\"opnsense\":{\"available\":false},");
    }

    jappend(&b, "\"fan\":{\"running\":%s,\"mode\":", s.fan_running ? "true" : "false");
    if (s.fan_mode[0]) jstr(&b, s.fan_mode); else jappend(&b, "null");
    if (s.fan_cpu_temp >= 0) jappend(&b, ",\"cpu_temp\":%d", s.fan_cpu_temp); else jappend(&b, ",\"cpu_temp\":null");
    if (s.fan_sys_temp >= 0) jappend(&b, ",\"sys_temp\":%d", s.fan_sys_temp); else jappend(&b, ",\"sys_temp\":null");
    if (s.fan_cpu_pct >= 0)  jappend(&b, ",\"cpu_pct\":%d", s.fan_cpu_pct);  else jappend(&b, ",\"cpu_pct\":null");
    if (s.fan_sys_pct >= 0)  jappend(&b, ",\"sys_pct\":%d", s.fan_sys_pct);  else jappend(&b, ",\"sys_pct\":null");
    jappend(&b, ",\"rpm\":{");
    const char *rn[4] = { "cpufan1", "cpufan2", "sysfan1", "sysfan2" };
    for (int i = 0; i < 4; i++) {
        if (s.fan_rpm[i] >= 0) jappend(&b, "%s\"%s\":%ld", i ? "," : "", rn[i], s.fan_rpm[i]);
        else                   jappend(&b, "%s\"%s\":null", i ? "," : "", rn[i]);
    }
    jappend(&b, "},\"cpu_curve\":"); jcurve(&b, s.fan_cpu_curve);
    jappend(&b, ",\"sys_curve\":"); jcurve(&b, s.fan_sys_curve);
    jappend(&b, ",\"crit\":{\"cpu\":88,\"sys\":60}},");

    jappend(&b, "\"settings\":{\"brightness\":%d,\"backlight_timeout\":%d,\"sleep_brightness\":%d,"
                "\"leds_on\":%s,\"led_night\":%s,\"language\":",
            s.brightness, s.backlight_timeout, s.sleep_brightness,
            s.leds_on ? "true" : "false", s.led_night ? "true" : "false");
    jstr(&b, s.language);
    jappend(&b, ",\"wallpaper\":"); jstr(&b, s.wallpaper);
    jappend(&b, ",\"led_night_window\":"); jstr(&b, s.led_night_window);
    jappend(&b, "},");

    jappend(&b, "\"wallpapers\":{\"current\":");
    jstr(&b, (s.wp_cur >= 0 && s.wp_cur < s.wp_count) ? s.wp_opts[s.wp_cur] : "");
    jappend(&b, ",\"options\":[");
    for (int i = 0; i < s.wp_count; i++) { jappend(&b, "%s", i ? "," : ""); jstr(&b, s.wp_opts[i]); }
    jappend(&b, "]},");

    jappend(&b, "\"caps\":{\"has_pve\":%s,\"has_opnsense\":%s,\"has_leds\":%s,\"has_gpu\":%s,\"has_touch\":%s}}",
            s.has_pve ? "true" : "false", s.has_opnsense ? "true" : "false",
            s.has_leds ? "true" : "false", s.has_gpu ? "true" : "false", s.has_touch ? "true" : "false");

    send_json(fd, 200, out);
}

static void handle_settings_get(int fd) {
    api_snapshot_t s;
    pthread_mutex_lock(&snap_lock);
    s = snap;
    pthread_mutex_unlock(&snap_lock);
    char out[512];
    jbuf_t b = { out, sizeof(out), 0 };
    jappend(&b, "{\"brightness\":%d,\"backlight_timeout\":%d,\"sleep_brightness\":%d,"
                "\"leds_on\":%s,\"led_night\":%s,\"has_leds\":%s,\"language\":",
            s.brightness, s.backlight_timeout, s.sleep_brightness,
            s.leds_on ? "true" : "false", s.led_night ? "true" : "false", s.has_leds ? "true" : "false");
    jstr(&b, s.language);
    jappend(&b, ",\"wallpaper\":"); jstr(&b, s.wallpaper);
    jappend(&b, ",\"led_night_window\":"); jstr(&b, s.led_night_window);
    jappend(&b, "}");
    send_json(fd, 200, out);
}

static void handle_wallpapers(int fd) {
    api_snapshot_t s;
    pthread_mutex_lock(&snap_lock);
    s = snap;
    pthread_mutex_unlock(&snap_lock);
    char out[512];
    jbuf_t b = { out, sizeof(out), 0 };
    jappend(&b, "{\"current\":");
    jstr(&b, (s.wp_cur >= 0 && s.wp_cur < s.wp_count) ? s.wp_opts[s.wp_cur] : "");
    jappend(&b, ",\"options\":[");
    for (int i = 0; i < s.wp_count; i++) { jappend(&b, "%s", i ? "," : ""); jstr(&b, s.wp_opts[i]); }
    jappend(&b, "]}");
    send_json(fd, 200, out);
}

static void handle_settings_post(int fd, const http_req_t *req) {
    const char *j = req->body ? req->body : "";
    int has_leds_cap;
    pthread_mutex_lock(&snap_lock);
    has_leds_cap = snap.has_leds;
    pthread_mutex_unlock(&snap_lock);

    api_settings_patch_t p;
    memset(&p, 0, sizeof(p));
    int v;
    char s[64];

    if (json_get_int(j, "brightness", &v) == 0) {
        if (v < 1 || v > 100) { send_error(fd, 400, "brightness 1-100"); return; }
        p.has_brightness = 1; p.brightness = v;
    }
    if (json_get_int(j, "backlight_timeout", &v) == 0) {
        if (v < 0) { send_error(fd, 400, "backlight_timeout >= 0"); return; }
        p.has_timeout = 1; p.timeout = v;
    }
    if (json_get_int(j, "sleep_brightness", &v) == 0) {
        if (v < 0 || v > 100) { send_error(fd, 400, "sleep_brightness 0-100"); return; }
        p.has_sleep = 1; p.sleep_brightness = v;
    }
    if (json_get_str(j, "language", s, sizeof(s)) == 0) {
        if (!valid_language(s)) { send_error(fd, 400, "unknown language"); return; }
        p.has_language = 1; snprintf(p.language, sizeof(p.language), "%.7s", s);
    }
    if (has_leds_cap && json_get_int(j, "leds_on", &v) == 0)   { p.has_leds_on = 1;  p.leds_on = !!v; }
    if (has_leds_cap && json_get_int(j, "led_night", &v) == 0) { p.has_led_night = 1; p.led_night = !!v; }
    if (json_get_str(j, "wallpaper", s, sizeof(s)) == 0) {
        if (!valid_wallpaper(s)) { send_error(fd, 400, "unknown wallpaper"); return; }
        p.has_wallpaper = 1; snprintf(p.wallpaper, sizeof(p.wallpaper), "%.31s", s);
    }

    if (!(p.has_brightness || p.has_timeout || p.has_sleep || p.has_language ||
          p.has_leds_on || p.has_led_night || p.has_wallpaper)) {
        send_error(fd, 400, "no changes");
        return;
    }
    api_apply_settings(&p);
    send_json(fd, 200, "{\"ok\":true}");
}

/* parse "points":[{"t":..,"p":..},...] */
static int parse_points(const char *j, int *temps, int *pcts, int *n) {
    const char *p = strstr(j, "\"points\"");
    if (!p) return -1;
    p = strchr(p, '[');
    if (!p) return -1;
    const char *end = strchr(p, ']');
    if (!end) return -1;
    *n = 0;
    while (p < end && *n < 12) {
        const char *t = strstr(p, "\"t\"");
        if (!t || t >= end) break;
        const char *c = strstr(t, "\"p\"");
        if (!c || c >= end) break;
        const char *tc = strchr(t, ':'), *pc = strchr(c, ':');
        if (!tc || !pc) break;
        int tv, pv;
        if (sscanf(tc + 1, "%d", &tv) != 1 || sscanf(pc + 1, "%d", &pv) != 1) break;
        temps[*n] = tv; pcts[*n] = pv; (*n)++;
        const char *brace = strchr(pc, '}');
        if (!brace) break;
        p = brace + 1;
    }
    return 0;
}

static int validate_curve(int *t, int *p, int n, char *err, size_t es) {
    if (n < 1 || n > 12) { snprintf(err, es, "curve needs 1-12 points"); return -1; }
    for (int i = 0; i < n; i++) {
        if (t[i] < 0 || t[i] > 120) { snprintf(err, es, "temperature out of range 0-120"); return -1; }
        if (p[i] < 0 || p[i] > 100) { snprintf(err, es, "speed out of range 0-100"); return -1; }
    }
    for (int i = 1; i < n; i++) {
        int kt = t[i], kp = p[i], j = i - 1;
        while (j >= 0 && t[j] > kt) { t[j + 1] = t[j]; p[j + 1] = p[j]; j--; }
        t[j + 1] = kt; p[j + 1] = kp;
    }
    for (int i = 1; i < n; i++)
        if (t[i] == t[i - 1]) { snprintf(err, es, "duplicate temperature"); return -1; }
    return 0;
}

static void handle_fan_mode(int fd, const http_req_t *req) {
    const char *j = req->body ? req->body : "";
    char mode[16];
    if (json_get_str(j, "mode", mode, sizeof(mode)) != 0) { send_error(fd, 400, "mode required"); return; }
    if (strcmp(mode, "silent") && strcmp(mode, "default") && strcmp(mode, "turbo")) {
        send_error(fd, 400, "invalid mode"); return;
    }

    char domain[8] = "";
    if (json_get_str(j, "domain", domain, sizeof(domain)) == 0) {
        if (strcmp(domain, "cpu") && strcmp(domain, "sys")) { send_error(fd, 400, "invalid domain"); return; }
        int temps[12], pcts[12], n = 0;
        if (parse_points(j, temps, pcts, &n) != 0) { send_error(fd, 400, "invalid points"); return; }
        char err[64];
        if (validate_curve(temps, pcts, n, err, sizeof(err)) != 0) { send_error(fd, 400, err); return; }
        char key[24], val[200];
        snprintf(key, sizeof(key), "%s_%s", domain, mode);
        int vlen = 0;
        for (int i = 0; i < n; i++)
            vlen += snprintf(val + vlen, sizeof(val) - vlen, "%s%d:%d", i ? "," : "", temps[i], pcts[i]);
        if (api_fand_set(key, val) != 0) { send_error(fd, 500, "write failed"); return; }
    }

    if (api_fand_set("mode", mode) != 0) { send_error(fd, 500, "write failed"); return; }
    send_json(fd, 200, "{\"ok\":true}");
}

static void handle_power(int fd, const http_req_t *req) {
    const char *j = req->body ? req->body : "";
    char action[16];
    int confirm = 0;
    json_get_int(j, "confirm", &confirm);
    if (json_get_str(j, "action", action, sizeof(action)) != 0) { send_error(fd, 400, "action required"); return; }
    int poweroff;
    if (!strcmp(action, "poweroff")) poweroff = 1;
    else if (!strcmp(action, "reboot")) poweroff = 0;
    else { send_error(fd, 400, "invalid action"); return; }
    if (!confirm) { send_error(fd, 400, "confirm required"); return; }

    send_json(fd, 200, "{\"ok\":true}");
    api_action_power(poweroff); /* response already flushed */
}

static void handle_wallpaper_upload(int fd, const http_req_t *req) {
    static const unsigned char png[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    if (!req->body || req->body_len < 8 || memcmp(req->body, png, 8) != 0) {
        send_error(fd, 400, "expected a PNG image"); return;
    }
    const char *tmp = "/etc/ug-paneld/wallpaper.png.tmp";
    const char *dst = "/etc/ug-paneld/wallpaper.png";
    int wfd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (wfd < 0) { send_error(fd, 500, "write failed"); return; }
    size_t off = 0;
    int ok = 1;
    while (off < req->body_len) {
        ssize_t w = write(wfd, req->body + off, req->body_len - off);
        if (w <= 0) { ok = 0; break; }
        off += (size_t)w;
    }
    fsync(wfd);
    close(wfd);
    if (!ok || rename(tmp, dst) != 0) { unlink(tmp); send_error(fd, 500, "write failed"); return; }

    api_cmd_t c = { .type = API_CMD_WALLPAPER_RESCAN };
    api_cmd_push(&c);
    send_json(fd, 200, "{\"ok\":true,\"wallpaper\":\"custom\"}");
}

/* legacy /backlight (kept for the Home Assistant light entity) */
static void handle_backlight_get(int fd) {
    char body[128];
    snprintf(body, sizeof(body), "{\"state\":\"%s\",\"brightness\":%d}",
             current_state ? "on" : "off", current_brightness);
    send_json(fd, 200, body);
}

static void handle_backlight_post(int fd, const char *body) {
    const char *s = strstr(body, "\"state\"");
    if (s) {
        if (strstr(s, "\"on\"")) { current_state = 1; backlight_set(current_brightness); }
        else if (strstr(s, "\"off\"")) { current_state = 0; backlight_off(); }
    }
    const char *bb = strstr(body, "\"brightness\"");
    if (bb) {
        bb += strlen("\"brightness\"");
        while (*bb == ':' || *bb == ' ') bb++;
        int val = (int)strtol(bb, NULL, 10);
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        current_brightness = val;
        if (val > 0) { current_state = 1; backlight_set(val); }
        else { current_state = 0; backlight_off(); }
    }
    handle_backlight_get(fd);
}

/* ---- router ---- */
static void handle_request(int fd) {
    http_req_t req;
    int err = read_request(fd, &req);
    if (err) { send_error(fd, err, NULL); return; }

    int is_get = strcmp(req.method, "GET") == 0;
    int is_post = strcmp(req.method, "POST") == 0;

    if (strcmp(req.path, "/healthz") == 0) {
        send_json(fd, 200, "{\"ok\":true}");
    } else if (strcmp(req.path, "/api/stats") == 0) {
        if (is_get) handle_stats(fd); else send_error(fd, 405, NULL);
    } else if (strcmp(req.path, "/api/settings") == 0) {
        if (is_get) handle_settings_get(fd);
        else if (is_post) {
            if (password_set() && !password_ok(&req)) send_auth_required(fd);
            else handle_settings_post(fd, &req);
        } else send_error(fd, 405, NULL);
    } else if (strcmp(req.path, "/api/wallpapers") == 0) {
        if (is_get) handle_wallpapers(fd); else send_error(fd, 405, NULL);
    } else if (strcmp(req.path, "/api/wallpaper") == 0) {
        if (!is_post) send_error(fd, 405, NULL);
        else if (password_set() && !password_ok(&req)) send_auth_required(fd);
        else handle_wallpaper_upload(fd, &req);
    } else if (strcmp(req.path, "/api/fan/mode") == 0) {
        if (!is_post) send_error(fd, 405, NULL);
        else if (password_set() && !password_ok(&req)) send_auth_required(fd);
        else handle_fan_mode(fd, &req);
    } else if (strcmp(req.path, "/api/power") == 0) {
        if (!is_post) send_error(fd, 405, NULL);
        else if (!password_set()) send_error(fd, 403, "set api_password to enable remote power control");
        else if (!password_ok(&req)) send_auth_required(fd);
        else handle_power(fd, &req);
    } else if (strcmp(req.path, "/backlight") == 0) {
        if (is_get) handle_backlight_get(fd);
        else if (is_post) handle_backlight_post(fd, req.body ? req.body : "");
        else send_error(fd, 405, NULL);
    } else if (is_get) {
        serve_file(fd, req.path);
    } else {
        send_error(fd, 404, "not found");
    }

    free(req.body);
}

static void *api_loop(void *arg) {
    (void)arg;
    while (api_running) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int fd = accept(server_fd, (struct sockaddr *)&client, &len);
        if (fd < 0) {
            if (api_running) {
                struct timespec ts = { .tv_nsec = 10000000 };
                nanosleep(&ts, NULL);
            }
            continue;
        }
        struct timeval tv = { .tv_sec = 3 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        handle_request(fd);
        close(fd);
    }
    return NULL;
}

/* ---- backlight bridge (unchanged) ---- */
void api_set_state(int on) { current_state = on; }
int  api_get_state(void) { return current_state; }
int  api_get_brightness(void) { return current_brightness; }
void api_set_brightness(int val) {
    if (val < 1) val = 1;
    if (val > 100) val = 100;
    current_brightness = val;
}

int api_start(int port, const char *password) {
    if (port <= 0) return -1;
    snprintf(api_password, sizeof(api_password), "%s", password ? password : "");

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Warning: API bind failed on port %d: %s\n", port, strerror(errno));
        close(server_fd);
        server_fd = -1;
        return -1;
    }
    if (listen(server_fd, 16) < 0) {
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    api_running = 1;
    if (pthread_create(&api_thread, NULL, api_loop, NULL) != 0) {
        api_running = 0;
        close(server_fd);
        server_fd = -1;
        return -1;
    }
    fprintf(stderr, "Web dashboard + API listening on port %d (%s)\n",
            port, password_set() ? "password set" : "no password — controls open on LAN");
    return 0;
}

void api_stop(void) {
    if (!api_running) return;
    api_running = 0;
    if (server_fd >= 0) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        server_fd = -1;
    }
    pthread_join(api_thread, NULL);
}
