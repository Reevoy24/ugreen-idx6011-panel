#!/bin/sh
# ug-fand launcher for TrueNAS SCALE (Post-Init) and Unraid (/boot/config/go).
# Lives next to the binary + config on persistent storage (a pool dataset on
# TrueNAS, the flash drive on Unraid) and is re-run on every boot, so it
# survives reboots and OS updates without relying on the ephemeral rootfs.
DIR="$(cd "$(dirname "$0")" && pwd)"

# config: the copy next to this script is the source of truth (/etc is ephemeral
# on TrueNAS/Unraid). Edit $DIR/config and re-run this script to apply.
mkdir -p /etc/ug-fand
[ -f "$DIR/config" ] && cp -f "$DIR/config" /etc/ug-fand/config

# On Unraid the binary sits on the FAT flash drive (no exec bit) — run it from a
# copy on the real rootfs. On TrueNAS the pool dataset is executable, run in place.
BIN="$DIR/ug-fand"
case "$DIR" in
    /boot/*) cp -f "$DIR/ug-fand" /usr/local/bin/ug-fand 2>/dev/null && chmod 755 /usr/local/bin/ug-fand && BIN=/usr/local/bin/ug-fand ;;
esac

# drivetemp exposes SATA disk temperatures via hwmon (the system-fan input).
# On Unraid it is only a fallback: ug-fand prefers emhttpd's disks.ini there,
# which costs no disk I/O and leaves spun-down drives asleep.
modprobe drivetemp 2>/dev/null

# Web dashboard (only active if api_port is set in the config). Serve the bundled
# frontend from here (the rootfs /usr/share is ephemeral on TrueNAS/Unraid), and
# mirror web config edits back to this persistent config so they survive reboots
# (the daemon writes /etc/ug-fand/config, which start.sh overwrites on boot).
[ -d "$DIR/web" ] && export UG_FAND_WEB_DIR="$DIR/web"
export UG_FAND_PERSIST="$DIR/config"

pkill -x ug-fand 2>/dev/null && sleep 1
nohup "$BIN" >/var/log/ug-fand.log 2>&1 &
echo "ug-fand started (log: /var/log/ug-fand.log)"
