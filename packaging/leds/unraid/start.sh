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
echo "Front LEDs set."
