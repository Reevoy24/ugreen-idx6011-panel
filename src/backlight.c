#include "backlight.h"
#include <stdio.h>

#define BACKLIGHT_PATH "/sys/devices/virtual/backlight/mipi_backlight/brightness"

// Inverted: 100% is 1, 0% is 198
#define BL_MIN 1
#define BL_MAX 198

static void backlight_write(int value)
{
    FILE *f = fopen(BACKLIGHT_PATH, "w");
    if (!f) {
        fprintf(stderr, "Warning: cannot open %s\n", BACKLIGHT_PATH);
        return;
    }
    fprintf(f, "%d", value);
    fclose(f);
}

void backlight_on(void)
{
    backlight_write(BL_MIN);
}

void backlight_off(void)
{
    backlight_write(BL_MAX);
}

void backlight_set(int percent)
{
    if (percent <= 0) {
        backlight_off();
        return;
    }
    if (percent >= 100) {
        backlight_on();
        return;
    }
    int value = BL_MAX - (percent * (BL_MAX - BL_MIN) / 100);
    backlight_write(value);
}
