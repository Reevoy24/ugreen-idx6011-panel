#ifndef SETTINGS_H
#define SETTINGS_H

#define STATE_FILE_PATH "/etc/ug-paneld/state.json"

/* Panel-adjustable settings, persisted separately from config.json so the
 * user's config file is never rewritten. State overrides config defaults. */
typedef struct {
    int brightness;        /* 1..100 */
    int backlight_timeout; /* seconds, 0 = never */
    char wallpaper[32];    /* "none", a built-in name, or "custom" */
    char language[4];      /* "de" / "en" */
} ui_state_t;

void settings_load(ui_state_t *st, int default_brightness, int default_timeout);
int settings_save(const ui_state_t *st);

#endif
