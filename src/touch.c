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
#include <dirent.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#define AXS_ADDR 0x3b
#define AXS_READ_LEN 8
#define DISP_W 258
#define DISP_H 960

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

#define I2C_HID_DRIVER_DIR "/sys/bus/i2c/drivers/i2c_hid_acpi"

/* Touchscreen ACPI ids seen on iDX6011 Pro units so far. Newer revisions
 * enumerate the same controller as MSFT8000 instead of CUST0000. */
static const char *known_touch_ids[] = { "CUST0000:00", "MSFT8000:00" };
#define KNOWN_TOUCH_ID_COUNT (sizeof(known_touch_ids) / sizeof(known_touch_ids[0]))

/* dev is the sysfs device name, e.g. "i2c-MSFT8000:00" */
static int unbind_one(const char *dev) {
    int fd = open(I2C_HID_DRIVER_DIR "/unbind", O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "i2c-hid: cannot open %s/unbind: %s\n",
                I2C_HID_DRIVER_DIR, strerror(errno));
        return -1;
    }

    ssize_t len = (ssize_t)strlen(dev);
    ssize_t n = write(fd, dev, len);
    close(fd);

    if (n != len) {
        fprintf(stderr, "i2c-hid: unbinding %s failed: %s\n", dev, strerror(errno));
        return -1;
    }
    fprintf(stderr, "i2c-hid: unbound %s — touchscreen freed for direct I2C access\n", dev);
    return 0;
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

    DIR *dir = opendir(I2C_HID_DRIVER_DIR);
    if (!dir) {
        fprintf(stderr, "i2c-hid: driver i2c_hid_acpi not present (blacklisted or not loaded) — nothing to unbind\n");
        return;
    }

    int bound = 0, unbound = 0, skipped = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "i2c-", 4) != 0)
            continue;
        bound++;

        const char *id = ent->d_name + 4;
        int match;
        if (autodetect) {
            match = 0;
            for (size_t i = 0; i < KNOWN_TOUCH_ID_COUNT; i++)
                if (strcasecmp(id, known_touch_ids[i]) == 0) { match = 1; break; }
        } else {
            match = (strcasecmp(id, acpi_id) == 0);
        }

        if (match) {
            if (unbind_one(ent->d_name) == 0)
                unbound++;
        } else {
            skipped++;
            fprintf(stderr, "i2c-hid: leaving %s bound (not the configured/known touchscreen; "
                            "set \"i2c_device\" in %s to override)\n", ent->d_name, CONFIG_FILE_PATH);
        }
    }
    closedir(dir);

    if (bound == 0)
        fprintf(stderr, "i2c-hid: no devices bound to i2c_hid_acpi — nothing to unbind\n");
    else if (unbound == 0 && !autodetect)
        fprintf(stderr, "i2c-hid: configured device %s is not bound to i2c_hid_acpi (already unbound?)\n", acpi_id);
    else if (unbound == 0 && autodetect && skipped > 0)
        fprintf(stderr, "i2c-hid: no known touchscreen id (CUST0000:00, MSFT8000:00) is bound; "
                        "check 'ls /sys/bus/i2c/devices/' for the id on your revision\n");
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
        if (resolve_touch_bus(resolved, sizeof(resolved)) == 0) {
            i2c_bus = resolved;
        } else {
            fprintf(stderr, "Touch: no known touchscreen ACPI device found, "
                            "falling back to /dev/i2c-2\n");
            i2c_bus = "/dev/i2c-2";
        }
    }

    i2c_fd = open(i2c_bus, O_RDWR);
    if (i2c_fd < 0) {
        fprintf(stderr, "Warning: Could not open I2C bus %s\n", i2c_bus);
        return -1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, AXS_ADDR) < 0) {
        fprintf(stderr, "Warning: Could not set I2C address 0x%02x\n", AXS_ADDR);
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

    uint8_t num = buf[1];
    uint8_t event = (buf[2] >> 6) & 0x03;

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

    uint16_t x = ((buf[2] & 0x0F) << 8) | buf[3];
    uint16_t y = ((buf[4] & 0x0F) << 8) | buf[5];

    // Reject outside bounds
    if (x >= DISP_W || y >= DISP_H)
        return 0;

    cur_x = x;
    cur_y = y;
    cur_pressed = 1;
    if (last_activity == 0)
        fprintf(stderr, "Touch: first contact detected at %u,%u — touch is working\n", x, y);
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
