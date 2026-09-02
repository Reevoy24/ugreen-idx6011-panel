#!/bin/sh
# UGREEN iDX6011 Pro front LEDs (static variant) — Unraid launcher.
# Called from /boot/config/go at boot (via sh; the flash drive is FAT and
# cannot hold exec bits). Copies the CLI off the flash, then stops the
# rolling boot animation and sets a static LED state. Colors, brightness
# and the activity thresholds live in ugreen-leds-mon.conf next to this
# script — edit that and re-run this script; an install never overwrites it.
PERSIST=/boot/config/ugreen-leds
BIN=/usr/local/bin/ugreen_leds_cli

mkdir -p /usr/local/bin
cp -f "$PERSIST/ugreen_leds_cli" "$BIN"
chmod 755 "$BIN"

COLOR_POWER="255 255 255"
BRIGHTNESS_POWER=144
COLOR_NETDEV_NORMAL="255 255 255"
BRIGHTNESS_NETDEV_LED=96
COLOR_DISK_HEALTH="255 255 255"
BRIGHTNESS_DISK_LEDS=64
[ -f "$PERSIST/ugreen-leds-mon.conf" ] && . "$PERSIST/ugreen-leds-mon.conf"

modprobe i2c-dev 2>/dev/null
modprobe i2c-i801 2>/dev/null

export UGREEN_MODEL=idx6011
# the color variables are "R G B" and must word-split into three arguments
"$BIN" power -on -color $COLOR_POWER -brightness "$BRIGHTNESS_POWER"
"$BIN" netdev netdev2 -on -color $COLOR_NETDEV_NORMAL -brightness "$BRIGHTNESS_NETDEV_LED"
"$BIN" disk1 disk2 disk3 disk4 disk5 disk6 -on -color $COLOR_DISK_HEALTH -brightness "$BRIGHTNESS_DISK_LEDS"

# live activity monitor: disk/network LEDs blink on activity via the MCU's
# hardware blink mode — pure userspace, no kernel module, survives updates.
# (it replaces a previously running instance via its pid file)
if [ -f "$PERSIST/ugreen-leds-mon.sh" ]; then
    nohup sh "$PERSIST/ugreen-leds-mon.sh" >/var/log/ugreen-leds-mon.log 2>&1 &
    echo "Front LEDs set, activity monitor running (log: /var/log/ugreen-leds-mon.log)."
else
    echo "Front LEDs set."
fi
