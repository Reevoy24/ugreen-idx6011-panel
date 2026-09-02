#!/bin/sh
# UGREEN iDX6011 Pro front LEDs (static variant) — TrueNAS SCALE launcher.
# Registered as a Post-Init script. Stops the rolling boot animation and
# sets a static LED state. Colors, brightness and the activity thresholds
# live in ugreen-leds-mon.conf next to this script — edit that and re-run
# this script; an install never overwrites it.
DIR="$(cd "$(dirname "$0")" && pwd)"
CLI="$DIR/ugreen_leds_cli"

COLOR_POWER="255 255 255"
BRIGHTNESS_POWER=144
COLOR_NETDEV_NORMAL="255 255 255"
BRIGHTNESS_NETDEV_LED=96
COLOR_DISK_HEALTH="255 255 255"
BRIGHTNESS_DISK_LEDS=64
[ -f "$DIR/ugreen-leds-mon.conf" ] && . "$DIR/ugreen-leds-mon.conf"

modprobe i2c-dev 2>/dev/null
modprobe i2c-i801 2>/dev/null

export UGREEN_MODEL=idx6011
# the color variables are "R G B" and must word-split into three arguments
"$CLI" power -on -color $COLOR_POWER -brightness "$BRIGHTNESS_POWER"
"$CLI" netdev netdev2 -on -color $COLOR_NETDEV_NORMAL -brightness "$BRIGHTNESS_NETDEV_LED"
"$CLI" disk1 disk2 disk3 disk4 disk5 disk6 -on -color $COLOR_DISK_HEALTH -brightness "$BRIGHTNESS_DISK_LEDS"

# live activity monitor: disk/network LEDs blink on activity via the MCU's
# hardware blink mode — pure userspace, no kernel module, survives updates.
# (it replaces a previously running instance via its pid file)
if [ -f "$DIR/ugreen-leds-mon.sh" ]; then
    nohup sh "$DIR/ugreen-leds-mon.sh" >/var/log/ugreen-leds-mon.log 2>&1 &
    echo "Front LEDs set, activity monitor running (log: /var/log/ugreen-leds-mon.log)."
else
    echo "Front LEDs set."
fi
