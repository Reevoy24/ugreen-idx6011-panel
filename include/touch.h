#ifndef TOUCH_H
#define TOUCH_H

/* Release the touchscreen from the i2c_hid_acpi kernel driver so we can talk
 * to it via /dev/i2c-N. acpi_id: "auto" (try known ids), "none" (skip), or a
 * specific id like "MSFT8000:00". Never fatal — touch is optional. */
void touch_unbind_i2c_hid(const char *acpi_id);

int touch_init(const char *i2c_bus);
int touch_is_pressed(void);
void touch_cleanup(void);

#endif
