#ifndef BUTTON_H
#define BUTTON_H

/* Front power button. On this hardware the chassis button is a standard ACPI
 * "Power Button" evdev (LNXPWRBN, KEY_POWER) — no vendor module needed. We grab
 * the device(s) with EVIOCGRAB so systemd-logind doesn't ALSO act on the key,
 * and report a press to the main loop, which runs the smart shutdown. If
 * ug-paneld exits the grab is released and logind's default poweroff resumes
 * (safe fallback).
 *
 * device: "auto" (default — scan /dev/input for "Power Button"/gpiokey devices
 * that emit KEY_POWER), "off"/"none" (disabled, leave the key to logind), or an
 * explicit /dev/input/eventN path. Returns the number of devices grabbed. */
int button_init(const char *device);

/* Non-blocking: drain pending events and return 1 if a power-button PRESS
 * happened since the last call, else 0. Call once per main-loop iteration. */
int button_poll(void);

#endif
