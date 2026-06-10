#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int json_get_int(const char *json, const char *key, int *value) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *found = strstr(json, search);
    if (!found) return -1;

    found += strlen(search);
    while (*found == ' ' || *found == '\t' || *found == '\n') found++;

    char *end;
    long v = strtol(found, &end, 10);
    if (end == found) return -1;

    *value = (int)v;
    return 0;
}

static int json_get_bool(const char *json, const char *key, int *value) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *found = strstr(json, search);
    if (!found) return -1;

    found += strlen(search);
    while (*found == ' ' || *found == '\t' || *found == '\n' || *found == '\r') found++;

    if (strncmp(found, "true", 4) == 0) { *value = 1; return 0; }
    if (strncmp(found, "false", 5) == 0) { *value = 0; return 0; }

    char *end;
    long v = strtol(found, &end, 10);
    if (end == found) return -1;

    *value = (v != 0);
    return 0;
}

static int json_get_str(const char *json, const char *key, char *buf, size_t buf_size) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *found = strstr(json, search);
    if (!found) return -1;

    found += strlen(search);
    while (*found == ' ' || *found == '\t' || *found == '\n') found++;
    if (*found != '"') return -1;
    found++;

    const char *end = strchr(found, '"');
    if (!end) return -1;

    size_t len = (size_t)(end - found);
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, found, len);
    buf[len] = '\0';
    return 0;
}

int config_load(config_t *config) {
    if (!config) return -1;

    config->poll_rate = DEFAULT_POLL_RATE;
    config->drm_device[0] = '\0';
    snprintf(config->connector, sizeof(config->connector), "auto");
    config->drm_probe_timeout = DEFAULT_DRM_PROBE_TIMEOUT;
    snprintf(config->i2c_device, sizeof(config->i2c_device), "auto");
    config->debug = 0;
    config->opnsense_url[0] = '\0';
    config->opnsense_key[0] = '\0';
    config->opnsense_secret[0] = '\0';
    snprintf(config->wan_interface, sizeof(config->wan_interface), "wan");
    config->wan_max_mbps = 1000;
    snprintf(config->touch_device, sizeof(config->touch_device), "auto");
    config->brightness = 100;
    config->backlight_timeout = 30;
    config->api_port = 0;

    FILE *fp = fopen(CONFIG_FILE_PATH, "r");
    if (!fp)
        return 0;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *json = malloc(size + 1);
    if (!json) {
        fclose(fp);
        return -1;
    }

    size_t nread = fread(json, 1, size, fp);
    json[nread] = '\0';
    fclose(fp);

    json_get_int(json, "poll_rate", &config->poll_rate);
    /* "drm_card" is the legacy key; "drm_device" wins if both are present */
    json_get_str(json, "drm_card", config->drm_device, sizeof(config->drm_device));
    json_get_str(json, "drm_device", config->drm_device, sizeof(config->drm_device));
    json_get_str(json, "connector", config->connector, sizeof(config->connector));
    json_get_int(json, "drm_probe_timeout", &config->drm_probe_timeout);
    json_get_str(json, "i2c_device", config->i2c_device, sizeof(config->i2c_device));
    json_get_bool(json, "debug", &config->debug);
    json_get_str(json, "opnsense_url", config->opnsense_url, sizeof(config->opnsense_url));
    json_get_str(json, "opnsense_key", config->opnsense_key, sizeof(config->opnsense_key));
    json_get_str(json, "opnsense_secret", config->opnsense_secret, sizeof(config->opnsense_secret));
    json_get_str(json, "wan_interface", config->wan_interface, sizeof(config->wan_interface));
    json_get_int(json, "wan_max_mbps", &config->wan_max_mbps);
    json_get_str(json, "touch_device", config->touch_device, sizeof(config->touch_device));
    json_get_int(json, "brightness", &config->brightness);
    json_get_int(json, "backlight_timeout", &config->backlight_timeout);
    json_get_int(json, "api_port", &config->api_port);

    free(json);
    return 0;
}
