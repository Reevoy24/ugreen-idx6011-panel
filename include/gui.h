#ifndef GUI_H
#define GUI_H

#include "lvgl/lvgl.h"
#include "system_stats.h"

lv_obj_t *gui_create_dashboard(void);
void gui_update_dashboard(lv_obj_t *screen, const system_stats_t *stats);
void gui_cleanup(void);

#endif
