#include "touch.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
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

int touch_init(const char *i2c_bus) {
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

    fprintf(stderr, "Touch input: %s addr 0x%02x\n", i2c_bus, AXS_ADDR);
    return 0;
}

int touch_is_pressed(void) {
    if (i2c_fd < 0) return 0;

    uint8_t buf[AXS_READ_LEN];

    if (write(i2c_fd, read_cmd, sizeof(read_cmd)) != sizeof(read_cmd))
        return 0;

    if (read(i2c_fd, buf, AXS_READ_LEN) != AXS_READ_LEN)
        return 0;

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

    return 1;
}

void touch_cleanup(void) {
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
}
