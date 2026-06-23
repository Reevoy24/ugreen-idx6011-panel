#ifndef SETTINGS_H
#define SETTINGS_H

#include "config.h"

#define STATE_FILE_PATH "/etc/ug-paneld/state.json"

/* Panel-adjustable settings, persisted separately from config.json so the
 * user's config file is never rewritten. State overrides config defaults. */
typedef struct {
    int brightness;          /* 1..100 */
    int backlight_timeout;   /* seconds, 0 = never */
    int sleep_brightness;    /* backlight %% while asleep; 0 = full off */
    char wallpaper[32];      /* "none", a built-in name, or "custom" */
    char language[4];        /* "de" / "en" */
    int leds_on;             /* front status LEDs master switch (default 1) */
    int led_night;           /* LEDs off during the configured night window */
    char led_night_start[8]; /* night window start, "HH:MM" */
    char led_night_end[8];   /* night window end, "HH:MM" */
    char timezone[40];       /* panel time zone; "" = system default */
} ui_state_t;

/* Defaults come from config.json; state.json (panel/web edits) overrides them. */
void settings_load(ui_state_t *st, const config_t *cfg);
int settings_save(const ui_state_t *st);

#endif
