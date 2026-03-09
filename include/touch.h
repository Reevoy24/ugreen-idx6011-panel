#ifndef TOUCH_H
#define TOUCH_H

int touch_init(const char *i2c_bus);
int touch_is_pressed(void);
void touch_cleanup(void);

#endif
