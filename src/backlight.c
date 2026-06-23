#include "backlight.h"
#include <stdio.h>
#include <sys/io.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

#define EC_SC   0x66
#define EC_DATA 0x62
#define EC_IBF  0x02
#define EC_ADDR_BACKLIGHT 0xA5

// Inverted scale: 1 = full brightness, 198 = off
#define BL_MIN 1
#define BL_MAX 198

static int ec_ready = 0;
static int ec_lock_fd = -1;   /* shared EC mutex with ug-fand (/run/ug-ec.lock) */

static int ec_wait_ibf_clear(void) {
    for (int i = 0; i < 5000; i++) {
        if (!(inb(EC_SC) & EC_IBF))
            return 0;
        struct timespec ts = { .tv_nsec = 100000 };
        nanosleep(&ts, NULL);
    }
    return -1;
}

static int ec_write(unsigned char addr, unsigned char val) {
    /* hold the shared EC lock for the whole transaction so a fan-daemon
     * access can't interleave with ours and scramble both */
    if (ec_lock_fd >= 0) flock(ec_lock_fd, LOCK_EX);
    int rc = -1;
    if (ec_wait_ibf_clear()) goto done;
    outb(0x81, EC_SC);
    if (ec_wait_ibf_clear()) goto done;
    outb(addr, EC_DATA);
    if (ec_wait_ibf_clear()) goto done;
    outb(val, EC_DATA);
    rc = 0;
done:
    if (ec_lock_fd >= 0) flock(ec_lock_fd, LOCK_UN);
    return rc;
}

int backlight_init(void) {
    if (iopl(3) < 0) {
        fprintf(stderr, "Warning: iopl failed, backlight unavailable (needs to run as root)\n");
        return -1;
    }
    /* serialize EC access with ug-fand, which writes the fan registers */
    ec_lock_fd = open("/run/ug-ec.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    ec_ready = 1;
    return 0;
}

static int backlight_write(int value) {
    if (!ec_ready) return -1;
    if (value < BL_MIN) value = BL_MIN;
    if (value > BL_MAX) value = BL_MAX;
    int rc = ec_write(EC_ADDR_BACKLIGHT, (unsigned char)value);
    if (rc != 0)
        fprintf(stderr, "Warning: EC backlight write failed\n");
    return rc;
}

void backlight_on(void) {
    backlight_write(BL_MIN);
}

void backlight_off(void) {
    backlight_write(BL_MAX);
}

int backlight_set_checked(int percent) {
    int value;
    if (percent <= 0)        value = BL_MAX;
    else if (percent >= 100) value = BL_MIN;
    else                     value = BL_MAX - (percent * (BL_MAX - BL_MIN) / 100);
    return backlight_write(value);
}

void backlight_set(int percent) {
    backlight_set_checked(percent);
}
