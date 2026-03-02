#ifndef DISPLAY_H
#define DISPLAY_H

#include "lvgl/lvgl.h"

int display_init(int *width, int *height);
lv_disp_t *display_get(void);
void display_render(void);
void display_close(void);

#endif
