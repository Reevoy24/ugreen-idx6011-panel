#!/bin/sh
# ug-hddtemp-pull.sh — feed ug-fand (and the panel) the temperatures of drives
# this host cannot see, because their controller is passed through to a VM.
#
# With VFIO passthrough the pool disks belong to the guest: the host has no
# /dev/sd* for them, no SMART, nothing for drivetemp — while the fans hang off
# the host's EC, which the guest cannot reach. Sensing and actuation end up on
# opposite sides of the passthrough. This script bridges them: it reads the
# temperatures inside the guest over ssh and writes them to the file the daemons
# read (disk_temp_file), so the fan curve follows the spinning disks again.
#
# Pull, not push: the credentials live on the host, the guest needs nothing but
# sshd and smartctl, and nothing in the guest gets a way into the host.
#
# Config (ug-fand /etc/ug-fand/config, and ug-paneld config.json if the panel
# should list the drives too):
#
#     disk_temp_file=/run/ug-fand/disk-temps
#     disk_temp_max_age=120
#
# Usage:
#   ug-hddtemp-pull.sh -t root@truenas.lan                  # one shot (cron/timer)
#   ug-hddtemp-pull.sh -t root@truenas.lan -i 60            # stay resident
#   ug-hddtemp-pull.sh -t root@truenas.lan -d "sda sdb sdc" # only these drives
#
#   -t user@host   guest to read from (or set UG_HDDTEMP_TARGET)
#   -o file        output file (default /run/ug-fand/disk-temps, UG_HDDTEMP_OUT)
#   -i seconds     repeat forever every N seconds (default: run once and exit)
#   -d "a b c"     kernel names to read (default: every sd*/nvme*n1 in the guest)
#
# As a systemd timer on the host (Proxmox), pulling every minute:
#
#   /etc/systemd/system/ug-hddtemp.service
#     [Unit]
#     Description=Pull HDD temperatures from the storage VM for ug-fand
#     [Service]
#     Type=oneshot
#     ExecStart=/usr/local/bin/ug-hddtemp-pull.sh -t root@truenas.lan
#   /etc/systemd/system/ug-hddtemp.timer
#     [Unit]
#     Description=Pull HDD temperatures every minute
#     [Timer]
#     OnBootSec=60
#     OnUnitActiveSec=60
#     [Install]
#     WantedBy=timers.target
#
#   systemctl enable --now ug-hddtemp.timer
#
# A failed pull deliberately writes NOTHING: the previous file then ages past
# disk_temp_max_age and ug-fand falls back to its missing-sensor failsafe (fans
# to 100%) rather than regulating on a frozen temperature.
#
# Prefer to push from inside the guest instead? The daemons take the same
# content on their web API, so one curl does it (needs api_port + api_password):
#   curl -u :PASSWORD --data-binary @- http://<host>:8765/api/disk-temps <<EOF
#   sda=41
#   sdb=39
#   EOF
set -eu

TARGET=${UG_HDDTEMP_TARGET:-}
OUT=${UG_HDDTEMP_OUT:-/run/ug-fand/disk-temps}
INTERVAL=0
DRIVES=""

while getopts "t:o:i:d:h" opt; do
    case "$opt" in
        t) TARGET=$OPTARG ;;
        o) OUT=$OPTARG ;;
        i) INTERVAL=$OPTARG ;;
        d) DRIVES=$OPTARG ;;
        h) sed -n '2,60p' "$0"; exit 0 ;;
        *) echo "try -h" >&2; exit 2 ;;
    esac
done

[ -n "$TARGET" ] || { echo "ug-hddtemp-pull: no target, use -t user@host" >&2; exit 2; }

# Runs INSIDE the guest. smartctl -n standby returns nothing for a sleeping
# drive, which is reported as "*" so the daemons leave it asleep instead of
# treating it as a dead sensor. Capacity comes from the block size in sectors.
remote_script() {
    printf 'want="%s"\n' "$DRIVES"      # the only value that crosses over
    cat <<'REMOTE'
set -u
if [ -n "$want" ]; then
    list=""
    for d in $want; do list="$list /sys/block/$d"; done
else
    list=$(ls -d /sys/block/sd* /sys/block/nvme*n1 2>/dev/null || true)
fi
for d in $list; do
    [ -e "$d/size" ] || continue
    n=$(basename "$d")
    t=$(smartctl -A -n standby -j "/dev/$n" 2>/dev/null | tr -d ' \n' |
        sed -n 's/.*"temperature":{"current":\([0-9]*\).*/\1/p')
    sec=$(cat "$d/size" 2>/dev/null || echo 0)
    tb=$(awk -v s="$sec" 'BEGIN { printf "%.2f", s * 512 / 1e12 }')
    printf '%s=%s,%s\n' "$n" "${t:-*}" "$tb"
done
REMOTE
}

pull_once() {
    mkdir -p "$(dirname "$OUT")"
    tmp="$OUT.tmp.$$"
    if ! remote_script | ssh -o BatchMode=yes -o ConnectTimeout=10 "$TARGET" sh > "$tmp" 2>/dev/null; then
        rm -f "$tmp"
        echo "ug-hddtemp-pull: cannot read $TARGET — leaving $OUT to go stale" >&2
        return 1
    fi
    if ! grep -q '=' "$tmp"; then
        rm -f "$tmp"
        echo "ug-hddtemp-pull: $TARGET reported no drives (is smartctl installed?)" >&2
        return 1
    fi
    mv "$tmp" "$OUT"          # atomic: a reader never sees a half-written file
    return 0
}

if [ "$INTERVAL" -gt 0 ] 2>/dev/null; then
    while :; do
        pull_once || true
        sleep "$INTERVAL"
    done
fi

pull_once
