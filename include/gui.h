#ifndef GUI_H
#define GUI_H

#include "lvgl/lvgl.h"
#include "system_stats.h"
#include "opnsense.h"
#include "net_stats.h"
#include "disk_stats.h"
#include "pve_stats.h"

/* Multi-page dashboard (swipeable tileview): Home, Hardware, Netzwerk,
 * Festplatten, Proxmox (only when running on a PVE host), OPNsense (only
 * when configured). */
lv_obj_t *gui_create_dashboard(int show_opnsense, int show_pve, int wan_max_mbps);

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

void gui_cleanup(void);

#endif
