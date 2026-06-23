#ifndef API_H
#define API_H

#include <stddef.h>
#include "system_stats.h"
#include "net_stats.h"
#include "disk_stats.h"
#include "pve_stats.h"
#include "opnsense.h"

#define API_WP_MAX 10

/* Latest values the main loop publishes for the web API thread to read.
 * Copied under a mutex on both sides; the API never touches LVGL or the live
 * collectors. */
typedef struct {
    int valid;

    system_stats_t sys;
    int   has_gpu;   float gpu_usage;
    net_stats_t  net;
    disk_stats_t disks;
    int   has_pve;       pve_stats_t      pve;
    int   has_opnsense;  opnsense_stats_t opn;
    int   has_leds, has_touch;

    /* fan (from /run/ug-fand/status) */
    int  fan_running, fan_cpu_temp, fan_sys_temp, fan_cpu_pct, fan_sys_pct;
    long fan_rpm[4];
    char fan_mode[16], fan_cpu_curve[192], fan_sys_curve[192];

    /* current settings (copy of ui_state + derived) */
    int  brightness, backlight_timeout, sleep_brightness, leds_on, led_night;
    char language[8], wallpaper[32], led_night_window[16];
    char led_night_start[8], led_night_end[8], timezone[40];

    /* wallpaper options */
    int  wp_count, wp_cur;
    char wp_opts[API_WP_MAX][20];
} api_snapshot_t;

/* A partial settings update parsed from POST /api/settings (present-flags). */
typedef struct {
    int has_brightness, brightness;
    int has_timeout,    timeout;
    int has_sleep,      sleep_brightness;
    int has_language;   char language[8];
    int has_leds_on,    leds_on;
    int has_led_night,  led_night;
    int has_wallpaper;  char wallpaper[32];
    int has_night_start; char night_start[8];
    int has_night_end;   char night_end[8];
    int has_timezone;    char timezone[40];
} api_settings_patch_t;

/* GUI-affecting commands the API enqueues; the main loop drains + runs them on
 * the GUI thread. */
typedef enum {
    API_CMD_RETRANSLATE,
    API_CMD_WALLPAPER_SET,
    API_CMD_WALLPAPER_RESCAN,
    API_CMD_WALLPAPER_DELETE,
    API_CMD_LEDS_TOGGLE,
    API_CMD_LEDS_SET_NIGHT,
    API_CMD_SET_NIGHT_WINDOW,
    API_CMD_SET_TIMEZONE
} api_cmd_type_t;

typedef struct {
    api_cmd_type_t type;
    int  arg_int;
    char arg_str[48];
} api_cmd_t;

/* --- server lifecycle --- */
int  api_start(int port, const char *password); /* password "" = controls open on LAN */
void api_stop(void);

/* --- backlight bridge (used by the idle/sleep loop; unchanged) --- */
void api_set_brightness(int val);
int  api_get_brightness(void);
int  api_get_state(void);
void api_set_state(int on);

/* --- snapshot + command queue (main-loop side) --- */
void api_publish_stats(const api_snapshot_t *snap);
int  api_cmd_push(const api_cmd_t *c); /* 0 ok, -1 if the queue is full */
int  api_cmd_pop(api_cmd_t *out);      /* 1 popped, 0 if empty */

/* --- actions implemented in main.c, called by the api.c request handlers --- */
int  api_apply_settings(const api_settings_patch_t *p); /* applies inline + queues + persists; 0 ok */
int  api_fand_set(const char *key, const char *value);  /* write a key to /etc/ug-fand/config, preserving others */
void api_action_power(int poweroff);                    /* backlight off + reboot/poweroff */

#endif
