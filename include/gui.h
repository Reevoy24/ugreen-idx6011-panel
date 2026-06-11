#ifndef GUI_H
#define GUI_H

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
    void (*set_brightness)(int pct);
    void (*set_timeout)(int seconds);
    void (*do_reboot)(void);
    void (*do_poweroff)(void);
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

int  gui_page_count(void);
void gui_show_page(int idx);
void gui_settings_open(void);
void gui_settings_close(void);
void gui_set_sleep(int on);   /* black overlay while the screen "sleeps" */
void gui_leds_refresh(void);  /* re-read LED state into the panel rows */

void gui_cleanup(void);

#endif
