#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Where runtime settings are persisted. Default is the read/write rootfs path;
 * settings_load() repoints it from config.state_file or $UG_PANELD_STATE so
 * TrueNAS/Unraid can persist on the pool/flash. Both load and save use it. */
static char g_state_path[256] = STATE_FILE_PATH;

static int json_get_int(const char *json, const char *key, int *value)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *found = strstr(json, search);
    if (!found) return -1;
    found += strlen(search);
    while (*found == ' ' || *found == '\t' || *found == '\n' || *found == '\r') found++;
    char *end;
    long v = strtol(found, &end, 10);
    if (end == found) return -1;
    *value = (int)v;
    return 0;
}

static int json_get_str(const char *json, const char *key, char *buf, size_t buf_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *found = strstr(json, search);
    if (!found) return -1;
    found += strlen(search);
    while (*found == ' ' || *found == '\t' || *found == '\n' || *found == '\r') found++;
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

void settings_load(ui_state_t *st, const config_t *cfg)
{
    const char *env = getenv("UG_PANELD_STATE");
    if (cfg->state_file[0])  snprintf(g_state_path, sizeof(g_state_path), "%s", cfg->state_file);
    else if (env && env[0])  snprintf(g_state_path, sizeof(g_state_path), "%s", env);

    st->brightness = cfg->brightness;
    st->backlight_timeout = cfg->backlight_timeout;
    st->sleep_brightness = cfg->sleep_brightness;
    snprintf(st->wallpaper, sizeof(st->wallpaper), "%s", cfg->wallpaper); /* "" = auto: custom file if present, else none */
    snprintf(st->language, sizeof(st->language), "%s", cfg->language[0] ? cfg->language : "en");
    st->leds_on = !!cfg->leds_on;
    st->led_night = !!cfg->led_night;
    snprintf(st->led_night_start, sizeof(st->led_night_start), "%s", cfg->led_night_start);
    snprintf(st->led_night_end, sizeof(st->led_night_end), "%s", cfg->led_night_end);
    snprintf(st->timezone, sizeof(st->timezone), "%s", cfg->timezone);
    st->clock_24h = !!cfg->clock_24h;
    snprintf(st->storage_path, sizeof(st->storage_path), "%s",
             cfg->storage_path[0] ? cfg->storage_path : "/");

    FILE *fp = fopen(g_state_path, "r");
    if (!fp) return;

    char json[512];
    size_t n = fread(json, 1, sizeof(json) - 1, fp);
    json[n] = '\0';
    fclose(fp);

    json_get_int(json, "brightness", &st->brightness);
    json_get_int(json, "backlight_timeout", &st->backlight_timeout);
    json_get_int(json, "sleep_brightness", &st->sleep_brightness);
    json_get_str(json, "wallpaper", st->wallpaper, sizeof(st->wallpaper));
    json_get_str(json, "language", st->language, sizeof(st->language));
    json_get_int(json, "leds_on", &st->leds_on);
    json_get_int(json, "led_night", &st->led_night);
    json_get_str(json, "led_night_start", st->led_night_start, sizeof(st->led_night_start));
    json_get_str(json, "led_night_end", st->led_night_end, sizeof(st->led_night_end));
    json_get_str(json, "timezone", st->timezone, sizeof(st->timezone));
    json_get_int(json, "clock_24h", &st->clock_24h);
    st->clock_24h = !!st->clock_24h;
    json_get_str(json, "storage_path", st->storage_path, sizeof(st->storage_path));
    if (!st->storage_path[0]) snprintf(st->storage_path, sizeof(st->storage_path), "/");

    if (st->brightness < 0) st->brightness = 0;
    if (st->brightness > 100) st->brightness = 100;
    if (st->backlight_timeout < 0) st->backlight_timeout = 0;
    if (st->sleep_brightness < 0) st->sleep_brightness = 0;
    if (st->sleep_brightness > 100) st->sleep_brightness = 100;
    st->leds_on = !!st->leds_on;
    st->led_night = !!st->led_night;
}

int settings_save(const ui_state_t *st)
{
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_state_path);
    FILE *fp = fopen(tmp, "w");
    if (!fp) {
        fprintf(stderr, "settings: cannot write %s\n", tmp);
        return -1;
    }
    fprintf(fp,
            "{\n"
            "    \"brightness\": %d,\n"
            "    \"backlight_timeout\": %d,\n"
            "    \"sleep_brightness\": %d,\n"
            "    \"wallpaper\": \"%s\",\n"
            "    \"language\": \"%s\",\n"
            "    \"leds_on\": %d,\n"
            "    \"led_night\": %d,\n"
            "    \"led_night_start\": \"%s\",\n"
            "    \"led_night_end\": \"%s\",\n"
            "    \"timezone\": \"%s\",\n"
            "    \"clock_24h\": %d,\n"
            "    \"storage_path\": \"%s\"\n"
            "}\n",
            st->brightness, st->backlight_timeout, st->sleep_brightness,
            st->wallpaper, st->language, st->leds_on, st->led_night,
            st->led_night_start, st->led_night_end, st->timezone, st->clock_24h,
            st->storage_path);
    fclose(fp);

    if (rename(tmp, g_state_path) != 0) {
        fprintf(stderr, "settings: rename to %s failed\n", g_state_path);
        unlink(tmp);
        return -1;
    }
    return 0;
}
