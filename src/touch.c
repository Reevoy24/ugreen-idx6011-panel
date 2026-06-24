#include "touch.h"
#include "config.h"
#include "include/custom_tick.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#define AXS_ADDR 0x3b
#define AXS_READ_LEN 8
/* The touch controller reports coordinates in its own fixed native grid, which
 * matches the panel's native 258x960 mode. touch_poll() scales these into the
 * live LVGL resolution (lv_display_get_*_resolution), so this is the SOURCE
 * coordinate space — not necessarily the current on-screen size. */
#define TOUCH_NATIVE_W 258
#define TOUCH_NATIVE_H 960

static const uint8_t read_cmd[8] = {
    0xB5, 0xAB, 0xA5, 0x5A,
    0x00, 0x00, 0x00, AXS_READ_LEN
};

static int i2c_fd = -1;
static int fail_count = 0;
#define MAX_FAILURES 50
#define RECONNECT_MS 5000

/* remembered for automatic reconnects after transient I2C failures */
static char bus_path[32];
static uint32_t disabled_at = 0;

/* verbose touch logging (config "debug"); off by default */
static int touch_debug = 0;
void touch_set_debug(int on) { touch_debug = on; }

/* Touchscreen ACPI ids seen on iDX6011 Pro units so far. Newer revisions
 * enumerate the same controller as MSFT8000 instead of CUST0000. */
static const char *known_touch_ids[] = { "CUST0000:00", "MSFT8000:00" };
#define KNOWN_TOUCH_ID_COUNT (sizeof(known_touch_ids) / sizeof(known_touch_ids[0]))

/* Release one touch client from whatever driver currently owns it by writing
 * its kernel device name to /sys/bus/i2c/devices/i2c-<id>/driver/unbind. Going
 * through the device's own "driver" symlink avoids hard-coding a driver name:
 * it reaches i2c_hid_acpi (mainline) just as well as i2c_hid or a built-in
 * HID-over-I2C driver. Slackware/Unraid kernels name that driver differently
 * or build it in, so /sys/bus/i2c/drivers/i2c_hid_acpi may not exist at all —
 * the old driver-dir approach silently gave up there and left the client
 * bound, so the later ioctl(I2C_SLAVE, 0x3b) failed with EBUSY.
 *
 * id is the ACPI id without the sysfs "i2c-" prefix, e.g. "MSFT8000:00".
 * Returns 1 if the client is present and now free for direct I2C (unbound, or
 * already had no driver), 0 if no such device exists, -1 on a real error. */
static int unbind_one(const char *id) {
    char devdir[128];
    snprintf(devdir, sizeof(devdir), "/sys/bus/i2c/devices/i2c-%s", id);
    if (access(devdir, F_OK) != 0)
        return 0;   /* this controller is not present on this revision */

    /* the unbind attribute matches the device's kernel name, which is the
     * device-dir basename — i.e. the id with the "i2c-" prefix kept */
    char devname[64];
    snprintf(devname, sizeof(devname), "i2c-%s", id);

    char unbind_path[160];
    snprintf(unbind_path, sizeof(unbind_path), "%s/driver/unbind", devdir);

    int fd = open(unbind_path, O_WRONLY);
    if (fd < 0) {
        /* no "driver" symlink (ENOENT) means nothing owns the client — it is
         * already free for direct I2C, which is success, not an error */
        if (errno == ENOENT) {
            fprintf(stderr, "i2c-hid: %s has no bound driver — already free for direct I2C\n", devname);
            return 1;
        }
        fprintf(stderr, "i2c-hid: cannot open %s: %s\n", unbind_path, strerror(errno));
        return -1;
    }

    ssize_t len = (ssize_t)strlen(devname);
    ssize_t n = write(fd, devname, len);
    close(fd);

    if (n != len) {
        fprintf(stderr, "i2c-hid: unbinding %s failed: %s\n", devname, strerror(errno));
        return -1;
    }
    fprintf(stderr, "i2c-hid: unbound %s — touchscreen freed for direct I2C access\n", devname);
    return 1;
}

void touch_unbind_i2c_hid(const char *acpi_id) {
    if (acpi_id && strcasecmp(acpi_id, "none") == 0) {
        fprintf(stderr, "i2c-hid: unbind disabled by config (i2c_device=none)\n");
        return;
    }
    int autodetect = (!acpi_id || !acpi_id[0] || strcasecmp(acpi_id, "auto") == 0);
    /* accept the id with or without the sysfs "i2c-" prefix */
    if (!autodetect && strncmp(acpi_id, "i2c-", 4) == 0)
        acpi_id += 4;

    if (!autodetect) {
        if (unbind_one(acpi_id) == 0)
            fprintf(stderr, "i2c-hid: configured device i2c-%s not found under "
                            "/sys/bus/i2c/devices/ — check 'ls /sys/bus/i2c/devices/' "
                            "for the id on your revision\n", acpi_id);
        return;
    }

    int present = 0;
    for (size_t i = 0; i < KNOWN_TOUCH_ID_COUNT; i++)
        if (unbind_one(known_touch_ids[i]) != 0)
            present++;

    if (present == 0)
        fprintf(stderr, "i2c-hid: no known touchscreen device present "
                        "(checked CUST0000:00, MSFT8000:00) — check "
                        "'ls /sys/bus/i2c/devices/' for the id on your revision and "
                        "set \"i2c_device\" in %s\n", CONFIG_FILE_PATH);
}

/* The touchscreen's I2C bus number shifts between hardware revisions and
 * driver load order. The ACPI device symlink names the real bus:
 *   /sys/bus/i2c/devices/i2c-CUST0000:00 -> .../i2c-1/i2c-CUST0000:00 */
static int resolve_touch_bus(char *out, size_t out_size) {
    for (size_t i = 0; i < KNOWN_TOUCH_ID_COUNT; i++) {
        char link[96], target[256];
        snprintf(link, sizeof(link), "/sys/bus/i2c/devices/i2c-%s", known_touch_ids[i]);
        ssize_t n = readlink(link, target, sizeof(target) - 1);
        if (n <= 0) continue;
        target[n] = '\0';

        /* strip the device component, the parent component is the bus */
        char *slash = strrchr(target, '/');
        if (!slash) continue;
        *slash = '\0';
        slash = strrchr(target, '/');
        const char *bus_part = slash ? slash + 1 : target;

        int bus;
        if (sscanf(bus_part, "i2c-%d", &bus) == 1) {
            snprintf(out, out_size, "/dev/i2c-%d", bus);
            fprintf(stderr, "Touch: %s found on %s\n", known_touch_ids[i], out);
            return 0;
        }
    }
    return -1;
}

int touch_init(const char *i2c_bus) {
    char resolved[32];
    if (!i2c_bus || !i2c_bus[0] || strcasecmp(i2c_bus, "auto") == 0) {
        if (resolve_touch_bus(resolved, sizeof(resolved)) != 0) {
            /* The touchscreen's I2C bus is not present. Its ACPI device
             * (CUST0000/MSFT8000) can exist, but without an I2C host adapter it
             * never becomes a usable bus — which is the case when the kernel
             * lacks the Intel LPSS / DesignWare I2C driver (i2c-designware,
             * intel-lpss), as on stock Unraid. Do NOT guess a bus here: blindly
             * opening /dev/i2c-2 lands on an unrelated adapter (e.g. an i915
             * GMBUS) and spins forever in I2C-failure/reconnect logging. Disable
             * touch cleanly instead; the display and LEDs are unaffected and the
             * dashboard simply stays on (nothing could wake it from sleep
             * anyway). A "touch_device" set explicitly in the config overrides
             * this and is opened directly below. */
            fprintf(stderr, "Touch: no touchscreen I2C bus found — the kernel is "
                            "missing the Intel LPSS / DesignWare I2C driver "
                            "(expected on stock Unraid). Display and LEDs work; "
                            "touch is disabled. Set \"touch_device\" to a "
                            "/dev/i2c-N to override.\n");
            return -1;
        }
        i2c_bus = resolved;
    }

    i2c_fd = open(i2c_bus, O_RDWR);
    if (i2c_fd < 0) {
        fprintf(stderr, "Warning: Could not open I2C bus %s: %s\n", i2c_bus, strerror(errno));
        return -1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, AXS_ADDR) < 0) {
        /* EBUSY here means a kernel driver still owns address 0x3b — the
         * i2c-hid unbind above did not take effect (driver built-in or named
         * differently, and not blacklisted). Any other errno points at the
         * wrong or empty I2C bus. Printing it makes the log self-diagnosing. */
        fprintf(stderr, "Warning: Could not set I2C address 0x%02x on %s: %s\n",
                AXS_ADDR, i2c_bus, strerror(errno));
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    snprintf(bus_path, sizeof(bus_path), "%s", i2c_bus);
    fprintf(stderr, "Touch input: %s addr 0x%02x\n", i2c_bus, AXS_ADDR);
    return 0;
}

/* After too many I2C failures the fd is closed; the chip may just have lost
 * power briefly (the EC cuts the panel rail at very low backlight levels),
 * so keep retrying instead of giving up forever. */
static void touch_try_reconnect(void) {
    uint32_t now = custom_tick_get();
    if (!bus_path[0] || now - disabled_at < RECONNECT_MS)
        return;
    disabled_at = now;

    int fd = open(bus_path, O_RDWR);
    if (fd < 0) return;
    if (ioctl(fd, I2C_SLAVE, AXS_ADDR) < 0) {
        close(fd);
        return;
    }
    i2c_fd = fd;
    fail_count = 0;
    fprintf(stderr, "Touch: reconnected on %s\n", bus_path);
}

/* cached state, refreshed by touch_poll() */
static uint16_t cur_x = 0, cur_y = 0;
static int cur_pressed = 0;
static uint32_t last_activity = 0;

/* Live LVGL resolution, used to scale the controller's native coordinates onto
 * the screen. Initialised to the native grid so the mapping is an exact
 * identity until the real display size is known, then resolved once from LVGL's
 * default display on the first accepted contact. The panel normally comes up at
 * its native 258x960 (identity), but if the DRM connector binds a different
 * mode — e.g. when an external GPU perturbs connector/mode selection — an
 * unscaled 1:1 map would confine touches to a sub-rectangle of the screen. */
static int32_t disp_w = TOUCH_NATIVE_W, disp_h = TOUCH_NATIVE_H;
static int disp_resolved = 0;

static void touch_resolve_disp_size(void) {
    if (disp_resolved) return;
    lv_display_t *d = lv_display_get_default();
    if (!d) return;
    int32_t w = lv_display_get_horizontal_resolution(d);
    int32_t h = lv_display_get_vertical_resolution(d);
    if (w <= 0 || h <= 0) return;   /* not ready yet — keep the identity default */
    disp_w = w; disp_h = h; disp_resolved = 1;
    if (disp_w != TOUCH_NATIVE_W || disp_h != TOUCH_NATIVE_H)
        fprintf(stderr, "Touch: display %dx%d differs from native %dx%d — "
                "scaling touch input to match\n",
                disp_w, disp_h, TOUCH_NATIVE_W, TOUCH_NATIVE_H);
}

int touch_poll(void) {
    cur_pressed = 0;
    if (i2c_fd < 0) {
        touch_try_reconnect();
        if (i2c_fd < 0) return 0;
    }

    uint8_t buf[AXS_READ_LEN];

    if (write(i2c_fd, read_cmd, sizeof(read_cmd)) != sizeof(read_cmd)) {
        if (++fail_count >= MAX_FAILURES) {
            fprintf(stderr, "Touch: too many I2C failures, retrying every %d s\n",
                    RECONNECT_MS / 1000);
            close(i2c_fd);
            i2c_fd = -1;
            disabled_at = custom_tick_get();
        }
        return 0;
    }

    if (read(i2c_fd, buf, AXS_READ_LEN) != AXS_READ_LEN) {
        if (++fail_count >= MAX_FAILURES) {
            fprintf(stderr, "Touch: too many I2C failures, retrying every %d s\n",
                    RECONNECT_MS / 1000);
            close(i2c_fd);
            i2c_fd = -1;
            disabled_at = custom_tick_get();
        }
        return 0;
    }

    fail_count = 0;

    /* With debug enabled, dump the raw frame (throttled) BEFORE the filters.
     * This is the only way to tell "reads succeed but every frame is rejected"
     * (wrong bus, or a firmware whose byte layout differs) from "touch never
     * initialised". Off by default so it never floods a working box. */
    if (touch_debug) {
        static uint32_t last_dump = 0;
        uint32_t tnow = custom_tick_get();
        if ((int32_t)(tnow - last_dump) >= 1000) {
            last_dump = tnow;
            fprintf(stderr, "Touch raw: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        }
    }

    uint8_t num = buf[1];
    uint8_t event = (buf[2] >> 6) & 0x03;

    /* The chip returns constant 0x22/0x23 garbage whenever it is not physically
     * touched (num would be ~34) and a valid frame only during a real contact;
     * the checks below reject the garbage. */

    /* Only accept event 0 (down) or 2 (contact) with valid touch count
    Extracted from i2c bus using

    i2ctransfer -y 2 w8@0x3b 0xB5 0xAB 0xA5 0x5A 0x00 0x00 0x00 0x0E r14 

    Not touching:
    0x00 0x01 0x40 0x56 0x01 0x97 0x52 0x53 0xff 0xff 0xff 0xff 0xff 0xff 

    Touching around center:
    0x00 0x01 0x80 0x41 0x01 0x54 0x28 0x29 0xff 0xff 0xff 0xff 0xff 0xff 

    Not touching:
    0x00 0x01 0x40 0x33 0x01 0x52 0x58 0x59 0xff 0xff 0xff 0xff 0xff 0xff  

    Touching top left:
    0x00 0x01 0x80 0x1f 0x00 0x1e 0x27 0x28 0xff 0xff 0xff 0xff 0xff 0xff
    */
    
    if (num == 0 || (event != 0 && event != 2))
        return 0;

    /* raw coordinates in the controller's native grid */
    uint16_t rx = ((buf[2] & 0x0F) << 8) | buf[3];
    uint16_t ry = ((buf[4] & 0x0F) << 8) | buf[5];

    /* Reject frames outside the controller's native grid (the chip emits
     * constant garbage when untouched; those values fall outside). */
    if (rx >= TOUCH_NATIVE_W || ry >= TOUCH_NATIVE_H)
        return 0;

    /* Map the chip's native coordinates onto the live LVGL resolution. Identity
     * in the normal 258x960 case; only diverges if the panel bound a different
     * mode, which previously left touches stuck in a sub-rectangle (e.g. only
     * the upper half when the panel came up taller than 960). */
    touch_resolve_disp_size();
    cur_x = (uint16_t)((uint32_t)rx * (uint32_t)disp_w / TOUCH_NATIVE_W);
    cur_y = (uint16_t)((uint32_t)ry * (uint32_t)disp_h / TOUCH_NATIVE_H);
    cur_pressed = 1;
    static int first_logged = 0;
    if (!first_logged) {
        first_logged = 1;
        fprintf(stderr, "Touch: first contact at raw %u,%u -> mapped %u,%u — touch is working\n",
                rx, ry, cur_x, cur_y);
    }
    last_activity = custom_tick_get();
    return 1;
}

uint16_t touch_get_x(void) { return cur_x; }
uint16_t touch_get_y(void) { return cur_y; }
uint32_t touch_last_activity(void) { return last_activity; }

static void touch_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    touch_poll();
    data->state = cur_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = cur_x;
    data->point.y = cur_y;
}

void touch_lvgl_register(void) {
    if (i2c_fd < 0) return;
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_indev_read_cb);
    fprintf(stderr, "Touch registered as LVGL pointer device\n");
}

void touch_cleanup(void) {
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
}
