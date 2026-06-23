#ifndef SETTINGS_H
#define SETTINGS_H

#define STATE_FILE_PATH "/etc/ug-paneld/state.json"

/* Panel-adjustable settings, persisted separately from config.json so the
 * user's config file is never rewritten. State overrides config defaults. */
typedef struct {
    int brightness;        /* 1..100 */
    int backlight_timeout; /* seconds, 0 = never */
    int sleep_brightness;  /* backlight %% while asleep; 0 = full off */
    char wallpaper[32];    /* "none", a built-in name, or "custom" */
    char language[4];      /* "de" / "en" */
    int leds_on;           /* front status LEDs master switch (default 1) */
    int led_night;         /* LEDs off during the configured night window */
} ui_state_t;

void settings_load(ui_state_t *st, int default_brightness, int default_timeout,
                   int default_sleep_brightness, const char *default_language);
int settings_save(const ui_state_t *st);

#endif
