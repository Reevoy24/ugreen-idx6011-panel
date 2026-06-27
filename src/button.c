#include "button.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define MAX_BTN_FDS 8
#define BITS_PER_LONG  (8 * (int)sizeof(long))
#define NLONGS(x)      (((x) / BITS_PER_LONG) + 1)
#define TEST_BIT(b, a) (((a)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

static int btn_fds[MAX_BTN_FDS];
static int btn_count = 0;

/* the device can emit KEY_POWER */
static int has_key_power(int fd)
{
    unsigned long bits[NLONGS(KEY_MAX)];
    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0)
        return 0;
    return TEST_BIT(KEY_POWER, bits);
}

/* name is the ACPI power button or the UGOS GPIO key — never a keyboard, so we
 * don't accidentally grab (and swallow) a USB keyboard that also has KEY_POWER */
static int name_is_power_button(int fd)
{
    char name[256] = "";
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
        return 0;
    return strcmp(name, "Power Button") == 0 || strstr(name, "gpiokey") != NULL;
}

/* open + grab one device; returns the kept fd or -1. require_name gates the
 * auto-scan to real power buttons; an explicit path is trusted. */
static int try_grab(const char *path, int require_name)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    if (!has_key_power(fd) || (require_name && !name_is_power_button(fd))) {
        close(fd);
        return -1;
    }
    if (ioctl(fd, EVIOCGRAB, (void *)1) < 0)
        /* couldn't grab exclusively (someone else holds it) — still read it, but
         * logind may also act on the key */
        fprintf(stderr, "button: could not grab %s exclusively\n", path);
    return fd;
}

int button_init(const char *device)
{
    btn_count = 0;
    if (!device || !device[0] ||
        strcmp(device, "off") == 0 || strcmp(device, "none") == 0)
        return 0;

    if (strcmp(device, "auto") != 0) {
        char path[300];
        if (device[0] == '/')
            snprintf(path, sizeof(path), "%s", device);
        else
            snprintf(path, sizeof(path), "/dev/input/%s", device);
        int fd = try_grab(path, 0);
        if (fd >= 0) {
            btn_fds[btn_count++] = fd;
            fprintf(stderr, "button: power button = %s\n", path);
        } else {
            fprintf(stderr, "button: could not use %s\n", path);
        }
        return btn_count;
    }

    /* auto: scan /dev/input for "Power Button"/gpiokey devices with KEY_POWER */
    DIR *d = opendir("/dev/input");
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && btn_count < MAX_BTN_FDS) {
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = try_grab(path, 1);
        if (fd >= 0) {
            btn_fds[btn_count++] = fd;
            fprintf(stderr, "button: grabbed power button %s\n", path);
        }
    }
    closedir(d);
    if (btn_count == 0)
        fprintf(stderr, "button: no ACPI power-button device found\n");
    return btn_count;
}

int button_poll(void)
{
    int pressed = 0;
    struct input_event ev;
    for (int i = 0; i < btn_count; i++) {
        while (read(btn_fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1)
                pressed = 1;
        }
    }
    return pressed;
}
