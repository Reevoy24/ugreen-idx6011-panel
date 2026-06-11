#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

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

void settings_load(ui_state_t *st, int default_brightness, int default_timeout)
{
    st->brightness = default_brightness;
    st->backlight_timeout = default_timeout;
    st->wallpaper[0] = '\0'; /* "" = legacy auto: custom file if present, else none */
    snprintf(st->language, sizeof(st->language), "de");
    st->leds_on = 1;
    st->led_night = 0;

    FILE *fp = fopen(STATE_FILE_PATH, "r");
    if (!fp) return;

    char json[512];
    size_t n = fread(json, 1, sizeof(json) - 1, fp);
    json[n] = '\0';
    fclose(fp);

    json_get_int(json, "brightness", &st->brightness);
    json_get_int(json, "backlight_timeout", &st->backlight_timeout);
    json_get_str(json, "wallpaper", st->wallpaper, sizeof(st->wallpaper));
    json_get_str(json, "language", st->language, sizeof(st->language));
    json_get_int(json, "leds_on", &st->leds_on);
    json_get_int(json, "led_night", &st->led_night);

    if (st->brightness < 1) st->brightness = 1;
    if (st->brightness > 100) st->brightness = 100;
    if (st->backlight_timeout < 0) st->backlight_timeout = 0;
    st->leds_on = !!st->leds_on;
    st->led_night = !!st->led_night;
}

int settings_save(const ui_state_t *st)
{
    char tmp[] = STATE_FILE_PATH ".tmp";
    FILE *fp = fopen(tmp, "w");
    if (!fp) {
        fprintf(stderr, "settings: cannot write %s\n", tmp);
        return -1;
    }
    fprintf(fp,
            "{\n"
            "    \"brightness\": %d,\n"
            "    \"backlight_timeout\": %d,\n"
            "    \"wallpaper\": \"%s\",\n"
            "    \"language\": \"%s\",\n"
            "    \"leds_on\": %d,\n"
            "    \"led_night\": %d\n"
            "}\n",
            st->brightness, st->backlight_timeout, st->wallpaper, st->language,
            st->leds_on, st->led_night);
    fclose(fp);

    if (rename(tmp, STATE_FILE_PATH) != 0) {
        fprintf(stderr, "settings: rename to %s failed\n", STATE_FILE_PATH);
        unlink(tmp);
        return -1;
    }
    return 0;
}
