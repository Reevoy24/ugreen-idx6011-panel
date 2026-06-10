#ifndef DISPLAY_H
#define DISPLAY_H

#include "lvgl/lvgl.h"
#include "config.h"

/* display_init() return codes */
#define DISPLAY_OK            0
#define DISPLAY_ERR          -1 /* setup failed (possibly transient, worth a restart) */
#define DISPLAY_NO_CONNECTOR -2 /* kernel exposes no connected DRM connector (restart won't help) */

int display_init(const config_t *config);
void display_render(void);
void display_close(void);

#endif
