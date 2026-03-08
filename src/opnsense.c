#include "opnsense.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>

static char base_url[256];
static char api_key[256];
static char api_secret[256];
static char wan_iface[32];
static int initialized = 0;

typedef struct {
    char *data;
    size_t len;
} response_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    response_t *resp = userdata;
    size_t total = size * nmemb;
    char *tmp = realloc(resp->data, resp->len + total + 1);
    if (!tmp) return 0;
    resp->data = tmp;
    memcpy(resp->data + resp->len, ptr, total);
    resp->len += total;
    resp->data[resp->len] = '\0';
    return total;
}

static int api_get(const char *endpoint, response_t *resp) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char url[512];
    snprintf(url, sizeof(url), "%s%s", base_url, endpoint);

    resp->data = NULL;
    resp->len = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERNAME, api_key);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, api_secret);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(resp->data);
        resp->data = NULL;
        resp->len = 0;
        return -1;
    }
    return 0;
}

static const char *json_find_key(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    return p;
}

static int json_get_int_val(const char *json, const char *key) {
    const char *p = json_find_key(json, key);
    if (!p) return -1;
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return -1;
    return (int)v;
}

static float json_get_float_val(const char *json, const char *key) {
    const char *p = json_find_key(json, key);
    if (!p) return 0.0f;
    char *end;
    float v = strtof(p, &end);
    if (end == p) return 0.0f;
    return v;
}

static void json_get_str_val(const char *json, const char *key, char *buf, size_t buf_size) {
    const char *p = json_find_key(json, key);
    if (!p || *p != '"') { buf[0] = '\0'; return; }
    p++;
    const char *end = strchr(p, '"');
    if (!end) { buf[0] = '\0'; return; }
    size_t len = (size_t)(end - p);
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, p, len);
    buf[len] = '\0';
}

int opnsense_init(const config_t *config) {
    if (!config || !config->opnsense_url[0]) return -1;

    snprintf(base_url, sizeof(base_url), "%s", config->opnsense_url);
    snprintf(api_key, sizeof(api_key), "%s", config->opnsense_key);
    snprintf(api_secret, sizeof(api_secret), "%s", config->opnsense_secret);
    snprintf(wan_iface, sizeof(wan_iface), "%s", config->wan_interface);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    initialized = 1;
    return 0;
}

static void collect_gateway(opnsense_stats_t *stats) {
    response_t resp;
    if (api_get("/api/routes/gateway/status", &resp) != 0) {
        snprintf(stats->gw_status, sizeof(stats->gw_status), "Error");
        stats->gw_rtt_ms = -1;
        return;
    }

    json_get_str_val(resp.data, "status_translated", stats->gw_status, sizeof(stats->gw_status));
    if (stats->gw_status[0] == '\0')
        snprintf(stats->gw_status, sizeof(stats->gw_status), "Unknown");

    float delay = json_get_float_val(resp.data, "delay");
    stats->gw_rtt_ms = (int)(delay + 0.5f);

    free(resp.data);
}

static void collect_updates(opnsense_stats_t *stats) {
    response_t resp;
    if (api_get("/api/core/firmware/status", &resp) != 0) {
        snprintf(stats->update_status, sizeof(stats->update_status), "Unknown");
        return;
    }

    const char *sp = resp.data;
    const char *last_status = NULL;
    while ((sp = strstr(sp, "\"status\"")) != NULL) {
        last_status = sp;
        sp += 8;
    }

    char fw_status[32] = {0};
    if (last_status) {
        const char *v = last_status + 8;
        while (*v == ' ' || *v == '\t' || *v == ':') v++;
        if (*v == '"') {
            v++;
            const char *end = strchr(v, '"');
            if (end) {
                size_t len = (size_t)(end - v);
                if (len < sizeof(fw_status)) {
                    memcpy(fw_status, v, len);
                    fw_status[len] = '\0';
                }
            }
        }
    }

    if (strcmp(fw_status, "update") == 0)
        snprintf(stats->update_status, sizeof(stats->update_status), "Update available");
    else if (strcmp(fw_status, "none") == 0)
        snprintf(stats->update_status, sizeof(stats->update_status), "Up to date");
    else if (strcmp(fw_status, "error") == 0)
        snprintf(stats->update_status, sizeof(stats->update_status), "Error");
    else if (fw_status[0] != '\0')
        snprintf(stats->update_status, sizeof(stats->update_status), "%s", fw_status);
    else
        snprintf(stats->update_status, sizeof(stats->update_status), "Checking...");

    free(resp.data);
}

static void collect_dhcp(opnsense_stats_t *stats) {
    response_t resp;
    if (api_get("/api/dhcpv4/leases/searchLease", &resp) != 0) {
        stats->dhcp_leases = 0;
        return;
    }

    stats->dhcp_leases = json_get_int_val(resp.data, "total");
    if (stats->dhcp_leases < 0) stats->dhcp_leases = 0;

    free(resp.data);
}

static void collect_dns(opnsense_stats_t *stats) {
    response_t resp;
    if (api_get("/api/unbound/overview/totals/0", &resp) != 0) {
        stats->dns_queries = 0;
        stats->dns_blocked = 0;
        stats->dns_blocked_pct = 0;
        return;
    }

    stats->dns_queries = json_get_int_val(resp.data, "total");
    if (stats->dns_queries < 0) stats->dns_queries = 0;

    const char *blocked_section = strstr(resp.data, "\"blocked\"");
    if (blocked_section) {
        stats->dns_blocked = json_get_int_val(blocked_section, "total");
        char pct_str[16];
        json_get_str_val(blocked_section, "pcnt", pct_str, sizeof(pct_str));
        char *end;
        float pct = strtof(pct_str, &end);
        stats->dns_blocked_pct = (int)(pct + 0.5f);
    } else {
        stats->dns_blocked = 0;
        stats->dns_blocked_pct = 0;
    }
    if (stats->dns_blocked < 0) stats->dns_blocked = 0;

    free(resp.data);
}

static void collect_traffic(opnsense_stats_t *stats) {
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint),
             "/api/diagnostics/traffic/top/%s", wan_iface);

    response_t resp;
    if (api_get(endpoint, &resp) != 0) {
        stats->wan_in_bps = 0;
        stats->wan_out_bps = 0;
        return;
    }

    stats->wan_in_bps = json_get_float_val(resp.data, "rate_bits_in");
    stats->wan_out_bps = json_get_float_val(resp.data, "rate_bits_out");

    free(resp.data);
}

int opnsense_collect(opnsense_stats_t *stats) {
    if (!initialized || !stats) return -1;

    memset(stats, 0, sizeof(*stats));
    stats->gw_rtt_ms = -1;

    collect_gateway(stats);
    collect_updates(stats);
    collect_dhcp(stats);
    collect_dns(stats);
    collect_traffic(stats);

    return 0;
}

void opnsense_cleanup(void) {
    if (initialized) {
        curl_global_cleanup();
        initialized = 0;
    }
}
