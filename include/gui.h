#ifndef GUI_H
#define GUI_H

#include "lvgl/lvgl.h"
#include "system_stats.h"
#include "opnsense.h"

lv_obj_t *gui_create_dashboard(int show_opnsense, int wan_max_mbps);
void gui_update_dashboard(lv_obj_t *screen, const system_stats_t *stats);
void gui_update_opnsense(const opnsense_stats_t *stats);
void gui_update_wan_throughput(float wan_in_bps, float wan_out_bps);
void gui_update_clock(void);
void gui_cleanup(void);

#endif
