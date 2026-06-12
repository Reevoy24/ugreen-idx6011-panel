#!/bin/sh
# Front-LED uninstaller for Unraid (run as root).
PERSIST=/boot/config/ugreen-leds
GO=/boot/config/go

[ "$(id -u)" = "0" ] || { echo "Run as root." >&2; exit 1; }

[ -f /var/run/ugreen-leds-mon.pid ] && kill "$(cat /var/run/ugreen-leds-mon.pid)" 2>/dev/null

sed -i '/# >>> ugreen-leds >>>/,/# <<< ugreen-leds <<</d' "$GO" 2>/dev/null
rm -f /usr/local/bin/ugreen_leds_cli
rm -rf "$PERSIST"
echo "ugreen-leds removed."
echo "Note: after the next full power cycle the LEDs return to the stock rolling animation."
