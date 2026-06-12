#!/bin/sh
# UGREEN iDX6011 Pro front LEDs (static variant) — TrueNAS SCALE launcher.
# Registered as a Post-Init script. Stops the rolling boot animation and
# sets a static LED state. Edit the calls below to taste and re-run.
DIR="$(cd "$(dirname "$0")" && pwd)"
CLI="$DIR/ugreen_leds_cli"

modprobe i2c-dev 2>/dev/null
modprobe i2c-i801 2>/dev/null

export UGREEN_MODEL=idx6011
"$CLI" power -on -color 255 255 255 -brightness 144
"$CLI" netdev netdev2 -on -color 255 255 255 -brightness 96
"$CLI" disk1 disk2 disk3 disk4 disk5 disk6 -on -color 255 255 255 -brightness 64
echo "Front LEDs set."
