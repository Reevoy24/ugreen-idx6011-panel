#ifndef DISPLAY_H
#define DISPLAY_H

#include "lvgl/lvgl.h"

int display_init(const char *drm_card);
void display_render(void);
void display_close(void);

#endif
