#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

/* Release the touchscreen from whatever HID-over-I2C driver currently owns it
 * (i2c_hid_acpi, i2c_hid, or a built-in variant) so we can talk to it via
 * /dev/i2c-N. Resolves the client's own device dir and unbinds through its
 * "driver" symlink, so it works regardless of the driver's name. acpi_id:
 * "auto" (try known ids), "none" (skip), or a specific id like "MSFT8000:00".
 * Never fatal — touch is optional. */
void touch_unbind_i2c_hid(const char *acpi_id);

/* Enable verbose touch logging (raw I2C frame dumps). Driven by config "debug".
 * Call once at startup before touch_init/touch_poll. */
void touch_set_debug(int on);

int touch_init(const char *i2c_bus);

/* Single I2C reader: refreshes the cached touch state. Called by the LVGL
 * input device while awake and by the main loop while the screen sleeps. */
int touch_poll(void);              /* returns 1 while touched */
uint16_t touch_get_x(void);
uint16_t touch_get_y(void);
uint32_t touch_last_activity(void); /* custom_tick_get() ms of last contact */

/* Register the touchscreen as an LVGL pointer device (after display init). */
void touch_lvgl_register(void);

void touch_cleanup(void);

#endif
