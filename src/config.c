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

int config_load(config_t *config) {
    if (!config) return -1;

    config->refresh_rate = DEFAULT_REFRESH_RATE;
    config->fps = DEFAULT_FPS;
    config->show_temp = 1;
    config->show_uptime = 1;
    config->use_celsius = 1;

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

    fread(json, 1, size, fp);
    json[size] = '\0';
    fclose(fp);

    json_get_int(json, "refresh_rate", &config->refresh_rate);
    json_get_int(json, "fps", &config->fps);
    json_get_int(json, "show_temp", &config->show_temp);
    json_get_int(json, "show_uptime", &config->show_uptime);

    free(json);
    return 0;
}
