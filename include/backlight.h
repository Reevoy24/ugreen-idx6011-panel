#ifndef BACKLIGHT_H
#define BACKLIGHT_H

int backlight_init(void);
void backlight_on(void);
void backlight_off(void);
void backlight_set(int percent);

#endif
