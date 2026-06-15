#ifndef BACKLIGHT_H
#define BACKLIGHT_H

int backlight_init(void);
void backlight_on(void);
void backlight_off(void);
void backlight_set(int percent);
/* Like backlight_set but returns the EC write result: 0 = the EC accepted the
 * command (IBF cleared), -1 = it did not (EC busy/not ready, or not root).
 * Used to diagnose the cold-boot dark-panel timing. */
int backlight_set_checked(int percent);

#endif
