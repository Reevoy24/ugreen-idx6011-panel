#include "display.h"
#include "lv_drv_conf.h"
#include "lv_drivers/display/drm.h"
#include <stdio.h>
#include <stdlib.h>

#define DISP_BUF_ROWS 40

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

int display_init(int *width, int *height)
{
    lv_init();

    drm_init();

    lv_coord_t w, h;
    drm_get_sizes(&w, &h, NULL);

    if (width) *width = w;
    if (height) *height = h;

    uint32_t buf_size = w * DISP_BUF_ROWS;
    lv_color_t *buf1 = malloc(buf_size * sizeof(lv_color_t));
    lv_color_t *buf2 = malloc(buf_size * sizeof(lv_color_t));
    if (!buf1 || !buf2) {
        fprintf(stderr, "Failed to allocate display buffers\n");
        return -1;
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_size);

    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = drm_flush;
    disp_drv.wait_cb = drm_wait_vsync;
    disp_drv.hor_res = w;
    disp_drv.ver_res = h;

    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    if (!disp) {
        fprintf(stderr, "Failed to register display driver\n");
        return -1;
    }

    return 0;
}

lv_disp_t *display_get(void)
{
    return lv_disp_get_default();
}

void display_render(void)
{
    lv_task_handler();
}

void display_close(void)
{
    drm_exit();
}
