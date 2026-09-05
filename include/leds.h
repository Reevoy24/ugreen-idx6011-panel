#ifndef LEDS_H
#define LEDS_H

#include <time.h>

/* Front panel status LED control (the 9 RGB LEDs driven by the MCU at I2C
 * 0x3a — see tools/setup-ugreen-leds.sh). Two backends, detected at runtime:
 *
 *   sysfs — the led-ugreen kernel module is loaded: LEDs live under
 *           /sys/class/leds/{power,network_stat,network_stat2,disk1..6}.
 *           "off" stops ugreen-diskiomon and zeroes all LEDs; "on" restarts
 *           the monitor services, which restore triggers/colors/brightness.
 *   cli   — only ugreen_leds_cli exists (static-LED variant, e.g. without
 *           kernel headers): on/off through the CLI tool. "on" re-runs the
 *           install's start.sh so the configured colors come back; "off"
 *           stops the activity monitor first, or it would re-light the LEDs
 *           on the next disk burst. Looked for in /usr/local/bin, then on
 *           the Unraid flash drive and the TrueNAS pools (/usr is read-only
 *           there) — UG_PANELD_LEDS_DIR overrides the search.
 *
 * The night window (config: led_night_start/led_night_end) turns the LEDs
 * off between two times of day. Toggling them back on during the window
 * overrides it until the window ends; the next night they go off again. */

/* The three LED colors the settings UI exposes, as "R G B" strings. They
 * live in the LED config the active backend reads (/etc/ugreen-leds.conf for
 * the kernel module, ugreen-leds-mon.conf for the static install), so they
 * persist on their own and need no entry in state.json. Setting them writes
 * that file and re-applies, unless the LEDs are currently off. */
typedef struct {
    char power[16], disk[16], netdev[16];
} leds_colors_t;

int  leds_colors_supported(void);              /* 1 = a config we can edit */
void leds_get_colors(leds_colors_t *out);
int  leds_set_colors(const leds_colors_t *c);  /* 0 ok */

int  leds_init(const char *night_start, const char *night_end); /* 1 = controllable */
int  leds_available(void);

/* Apply the persisted user state once at daemon start. */
void leds_startup(int user_on, int night_enabled);

int  leds_user_on(void);        /* persisted intent (the master switch) */
int  leds_night_enabled(void);
int  leds_effective_on(void);   /* what the hardware should show right now */
const char *leds_night_window(void); /* e.g. "21:00-08:00" */

void leds_toggle(void);           /* LED row tapped */
void leds_set_night(int enabled); /* night mode row tapped */
void leds_set_window(const char *start, const char *end); /* change the night window times "HH:MM" */

/* Periodic re-evaluation (call ~once per second; cheap). Re-detects a late
 * backend (kernel module registering after daemon start) and applies night
 * window transitions. Returns 1 if the effective state changed. */
int  leds_tick(time_t now);

#endif
