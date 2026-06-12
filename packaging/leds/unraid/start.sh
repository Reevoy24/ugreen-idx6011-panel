#!/bin/sh
# UGREEN iDX6011 Pro front LEDs (static variant) — Unraid launcher.
# Called from /boot/config/go at boot (via sh; the flash drive is FAT and
# cannot hold exec bits). Copies the CLI off the flash, then stops the
# rolling boot animation and sets a static LED state. Edit to taste.
PERSIST=/boot/config/ugreen-leds
BIN=/usr/local/bin/ugreen_leds_cli

mkdir -p /usr/local/bin
cp -f "$PERSIST/ugreen_leds_cli" "$BIN"
chmod 755 "$BIN"

modprobe i2c-dev 2>/dev/null
modprobe i2c-i801 2>/dev/null

export UGREEN_MODEL=idx6011
"$BIN" power -on -color 255 255 255 -brightness 144
"$BIN" netdev netdev2 -on -color 255 255 255 -brightness 96
"$BIN" disk1 disk2 disk3 disk4 disk5 disk6 -on -color 255 255 255 -brightness 64

# live activity monitor: disk/network LEDs blink on activity via the MCU's
# hardware blink mode — pure userspace, no kernel module, survives updates.
# (it replaces a previously running instance via its pid file)
if [ -f "$PERSIST/ugreen-leds-mon.sh" ]; then
    nohup sh "$PERSIST/ugreen-leds-mon.sh" >/var/log/ugreen-leds-mon.log 2>&1 &
    echo "Front LEDs set, activity monitor running (log: /var/log/ugreen-leds-mon.log)."
else
    echo "Front LEDs set."
fi
