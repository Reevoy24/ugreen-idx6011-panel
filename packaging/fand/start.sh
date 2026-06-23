#!/bin/sh
# ug-fand TrueNAS SCALE launcher. Registered as a Post-Init script so it
# survives reboots and TrueNAS updates without touching the read-only rootfs.
# Lives next to the binary on a pool dataset.
DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p /etc/ug-fand
[ -f "$DIR/config" ] && cp -f "$DIR/config" /etc/ug-fand/config

# drivetemp = SATA disk temperatures (system-fan input)
modprobe drivetemp 2>/dev/null

pkill -x ug-fand 2>/dev/null && sleep 1
nohup "$DIR/ug-fand" >/var/log/ug-fand.log 2>&1 &
echo "ug-fand started (log: /var/log/ug-fand.log)"
