#!/bin/sh
# ug-paneld TrueNAS SCALE launcher. Registered as a Post-Init script so it
# survives reboots and TrueNAS updates without touching the read-only rootfs.
# Lives next to the binary on a pool dataset.
DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p /etc/ug-paneld
[ -f "$DIR/config.json" ] && cp -f "$DIR/config.json" /etc/ug-paneld/config.json
[ -f "$DIR/wallpaper.png" ] && cp -f "$DIR/wallpaper.png" /etc/ug-paneld/wallpaper.png

# the touchscreen needs the i2c-dev character devices
modprobe i2c-dev 2>/dev/null

pkill -x ug-paneld 2>/dev/null && sleep 1
nohup "$DIR/ug-paneld" >/var/log/ug-paneld.log 2>&1 &
echo "ug-paneld started (log: /var/log/ug-paneld.log)"
