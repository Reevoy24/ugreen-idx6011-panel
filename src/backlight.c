#include "backlight.h"
#include <stdio.h>
#include <sys/io.h>
#include <time.h>

#define EC_SC   0x66
#define EC_DATA 0x62
#define EC_IBF  0x02
#define EC_ADDR_BACKLIGHT 0xA5

// Inverted scale: 1 = full brightness, 198 = off
#define BL_MIN 1
#define BL_MAX 198

static int ec_ready = 0;

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
    if (ec_wait_ibf_clear()) return -1;
    outb(0x81, EC_SC);
    if (ec_wait_ibf_clear()) return -1;
    outb(addr, EC_DATA);
    if (ec_wait_ibf_clear()) return -1;
    outb(val, EC_DATA);
    return 0;
}

int backlight_init(void) {
    if (iopl(3) < 0) {
        fprintf(stderr, "Warning: iopl failed, backlight unavailable (needs to run as root)\n");
        return -1;
    }
    ec_ready = 1;
    return 0;
}

static void backlight_write(int value) {
    if (!ec_ready) return;
    if (value < BL_MIN) value = BL_MIN;
    if (value > BL_MAX) value = BL_MAX;
    if (ec_write(EC_ADDR_BACKLIGHT, (unsigned char)value) != 0)
        fprintf(stderr, "Warning: EC backlight write failed\n");
}

void backlight_on(void) {
    backlight_write(BL_MIN);
}

void backlight_off(void) {
    backlight_write(BL_MAX);
}

void backlight_set(int percent) {
    if (percent <= 0) { backlight_off(); return; }
    if (percent >= 100) { backlight_on(); return; }
    int value = BL_MAX - (percent * (BL_MAX - BL_MIN) / 100);
    backlight_write(value);
}
