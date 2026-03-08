#include "display.h"
#include "include/custom_tick.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static lv_display_t *disp = NULL;

// Scan /dev/dri for drm device
static char *find_drm_card(void)
{
    DIR *dir = opendir("/dev/dri");
    if (!dir) return NULL;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "card", 4) == 0) {
            char *path = malloc(256);
            snprintf(path, 256, "/dev/dri/%s", ent->d_name);
            closedir(dir);
            return path;
        }
    }
    closedir(dir);
    return NULL;
}

int display_init(void)
{
    lv_init();
    lv_tick_set_cb(custom_tick_get);

    disp = lv_linux_drm_create();
    if (!disp) {
        fprintf(stderr, "Failed to create DRM display\n");
        return -1;
    }

    char *path = find_drm_card();
    if (!path) {
        fprintf(stderr, "No DRM device found\n");
        return -1;
    }

    fprintf(stderr, "Using DRM device: %s\n", path);
    lv_linux_drm_set_file(disp, path, -1);
    free(path);

    int32_t w = lv_display_get_horizontal_resolution(disp);
    int32_t h = lv_display_get_vertical_resolution(disp);
    if (w == 0 || h == 0) {
        fprintf(stderr, "DRM display has no resolution — init failed\n");
        return -1;
    }

    fprintf(stderr, "DRM display initialized: %dx%d\n", w, h);
    return 0;
}

lv_display_t *display_get(void)
{
    return disp;
}

void display_render(void)
{
    lv_timer_handler();
}

void display_close(void)
{
    if (disp) {
        lv_display_delete(disp);
        disp = NULL;
    }
}
