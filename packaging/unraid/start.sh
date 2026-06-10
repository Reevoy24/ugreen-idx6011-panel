#!/bin/sh
# ug-paneld Unraid launcher. Called from /boot/config/go at boot (via sh, the
# flash drive is FAT and cannot hold exec bits). Copies the binary off the
# flash drive, syncs the config, and starts the daemon.
PERSIST=/boot/config/ug-paneld
BIN=/usr/local/bin/ug-paneld

mkdir -p /usr/local/bin /etc/ug-paneld /usr/share/ug-paneld/wallpapers
cp -f "$PERSIST/ug-paneld" "$BIN"
chmod 755 "$BIN"
[ -f "$PERSIST/config.json" ] && cp -f "$PERSIST/config.json" /etc/ug-paneld/config.json
[ -f "$PERSIST/state.json" ] && cp -f "$PERSIST/state.json" /etc/ug-paneld/state.json
[ -f "$PERSIST/wallpaper.png" ] && cp -f "$PERSIST/wallpaper.png" /etc/ug-paneld/wallpaper.png
[ -d "$PERSIST/wallpapers" ] && cp -f "$PERSIST/wallpapers/"*.png /usr/share/ug-paneld/wallpapers/ 2>/dev/null

# the touchscreen needs the i2c-dev character devices
modprobe i2c-dev 2>/dev/null

pkill -x ug-paneld 2>/dev/null && sleep 1
nohup "$BIN" >/var/log/ug-paneld.log 2>&1 &
echo "ug-paneld started (log: /var/log/ug-paneld.log)"
