#!/bin/sh
# ug-paneld TrueNAS SCALE launcher. Registered as a Post-Init script so it
# survives reboots and TrueNAS updates without touching the read-only rootfs.
# Lives next to the binary on a pool dataset.
DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p /etc/ug-paneld /usr/share/ug-paneld/wallpapers 2>/dev/null
[ -f "$DIR/config.json" ] && cp -f "$DIR/config.json" /etc/ug-paneld/config.json
[ -f "$DIR/wallpaper.png" ] && cp -f "$DIR/wallpaper.png" /etc/ug-paneld/wallpaper.png
[ -d "$DIR/wallpapers" ] && cp -f "$DIR/wallpapers/"*.png /usr/share/ug-paneld/wallpapers/ 2>/dev/null
# serve the web dashboard straight from the pool dir — /usr is read-only on TrueNAS,
# so copying into /usr/share/ug-paneld/web can't work there.
[ -d "$DIR/web" ] && export UG_PANELD_WEB_DIR="$DIR/web"

# the touchscreen needs the i2c-dev character devices
modprobe i2c-dev 2>/dev/null

# Persist runtime settings (panel/web edits) on the pool, not in ephemeral /etc,
# so brightness/language/wallpaper/LED/clock/timezone survive a reboot.
export UG_PANELD_STATE="$DIR/state.json"

pkill -x ug-paneld 2>/dev/null && sleep 1
nohup "$DIR/ug-paneld" >/var/log/ug-paneld.log 2>&1 &
echo "ug-paneld started (log: /var/log/ug-paneld.log)"

# fan control daemon (bundled) — monitors temps and drives the fans via the EC
if [ -f "$DIR/ug-fand" ]; then
    mkdir -p /etc/ug-fand 2>/dev/null
    # pool copy is the source of truth (/etc is ephemeral on TrueNAS); edit
    # $DIR/fand-config and re-run this script to apply.
    [ -f "$DIR/fand-config" ] && cp -f "$DIR/fand-config" /etc/ug-fand/config
    modprobe drivetemp 2>/dev/null   # exposes SATA drive temps as hwmon
    pkill -x ug-fand 2>/dev/null && sleep 1
    nohup "$DIR/ug-fand" >/var/log/ug-fand.log 2>&1 &
    echo "ug-fand started (log: /var/log/ug-fand.log)"
fi
