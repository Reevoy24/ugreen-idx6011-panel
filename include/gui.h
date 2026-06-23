#ifndef GUI_H
#define GUI_H

#include <pthread.h>
#include "lvgl/lvgl.h"
#include "system_stats.h"
#include "opnsense.h"
#include "net_stats.h"
#include "disk_stats.h"
#include "pve_stats.h"
#include "settings.h"

/* Wiring between the GUI (settings panel) and the daemon. All callbacks are
 * optional (NULL-safe) — the mock renderer passes none. */
typedef struct {
    int show_opnsense;
    int show_pve;
    int show_leds;                  /* front LED rows in the settings panel */
    int wan_max_mbps;
    ui_state_t *state;              /* shared; GUI mutates + persists it */
    pthread_mutex_t *settings_lock; /* guards *state across the GUI + API threads (optional) */
    void (*set_brightness)(int pct);
    void (*set_timeout)(int seconds);
    void (*do_reboot)(void);
    void (*do_poweroff)(void);
    void (*set_fan_mode)(const char *mode);  /* write ug-fand's mode (optional) */
} gui_setup_t;

/* Multi-page dashboard (swipeable tileview): Home, Hardware, Netzwerk,
 * Festplatten, Proxmox (only on PVE hosts), OPNsense (only when configured).
 * Swiping down from the top edge opens the settings panel. */
lv_obj_t *gui_create_dashboard(const gui_setup_t *setup);

void gui_update_clock(void);
void gui_update_dashboard(const system_stats_t *stats);
void gui_update_gpu(float usage_pct);           /* < 0 = unavailable */
void gui_update_net(const net_stats_t *net);
void gui_update_disks(const disk_stats_t *disks);
void gui_update_pve(const pve_stats_t *pve);
void gui_update_opnsense(const opnsense_stats_t *stats);
void gui_update_wan_throughput(float wan_in_bps, float wan_out_bps);
/* Fan page: temps in °C (<=0 = unknown), rpm[4]=cpufan1/2,sysfan1/2 (<0 = n/a),
 * mode = "silent"/"default"/"turbo" or NULL (daemon not running), cpu_curve /
 * sys_curve = "temp:pct,..." active-mode curves (NULL/"" = leave unchanged). */
void gui_update_fans(int cpu_temp, int sys_temp, const long rpm[4], const char *mode,
                     const char *cpu_curve, const char *sys_curve);

int  gui_page_count(void);
void gui_show_page(int idx);
void gui_settings_open(void);
void gui_settings_close(void);
void gui_set_sleep(int on);   /* black overlay while the screen "sleeps" */
void gui_leds_refresh(void);  /* re-read LED state into the panel rows */

/* Entry points the main loop calls (on the GUI thread) to apply web-API
 * changes that touch LVGL. Never call these from the API thread. */
void gui_retranslate(void);                  /* re-render all labels after a language change */
void gui_wallpaper_set(const char *name);    /* select + apply a wallpaper by name */
void gui_wallpaper_rescan(void);             /* re-scan options + re-apply current (after upload) */
/* Copy the current wallpaper option names (each <20 chars) for the web API.
 * Returns the count; *cur = selected index. */
int  gui_wallpaper_options(char out[][20], int max, int *cur);

void gui_cleanup(void);

#endif
