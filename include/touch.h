#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

/* Release the touchscreen from the i2c_hid_acpi kernel driver so we can talk
 * to it via /dev/i2c-N. acpi_id: "auto" (try known ids), "none" (skip), or a
 * specific id like "MSFT8000:00". Never fatal — touch is optional. */
void touch_unbind_i2c_hid(const char *acpi_id);

int touch_init(const char *i2c_bus);

/* Single I2C reader: refreshes the cached touch state. Called by the LVGL
 * input device while awake and by the main loop while the screen sleeps. */
int touch_poll(void);              /* returns 1 while touched */
uint16_t touch_get_x(void);
uint16_t touch_get_y(void);
uint32_t touch_last_activity(void); /* custom_tick_get() ms of last contact */

/* 1 when the last read was a structurally VALID frame (sane touch count) — the
 * chip is awake and sensing, not emitting its 0x23 auto-sleep garbage. Gates
 * the idle sleep so the screen only powers off when a tap can actually wake it
 * (arming on a bare I2C read, which garbage also satisfies, bricked v1.4.2). */
int touch_sense_ok(void);

/* Register the touchscreen as an LVGL pointer device (after display init). */
void touch_lvgl_register(void);

void touch_cleanup(void);

#endif
