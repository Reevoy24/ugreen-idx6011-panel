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
 *           kernel headers): on/off through the CLI tool.
 *
 * The night window (config: led_night_start/led_night_end) turns the LEDs
 * off between two times of day. Toggling them back on during the window
 * overrides it until the window ends; the next night they go off again. */

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

/* Periodic re-evaluation (call ~once per second; cheap). Re-detects a late
 * backend (kernel module registering after daemon start) and applies night
 * window transitions. Returns 1 if the effective state changed. */
int  leds_tick(time_t now);

#endif
