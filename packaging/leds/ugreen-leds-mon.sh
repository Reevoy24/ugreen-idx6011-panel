#!/bin/sh
# ugreen-leds-mon — userspace activity monitor for the UGREEN iDX6011 Pro
# front LEDs. No kernel module needed: disk and network activity is read
# from /proc//sys counters, and the LED MCU's own hardware blink mode does
# the animation. Only state TRANSITIONS touch the i2c bus, so steady state
# costs nothing but a few file reads per poll.
#
# Activity needs to cross a THRESHOLD before an LED starts blinking. Without
# one a single ARP packet or a ZFS metadata commit is enough to keep an idle
# NAS blinking forever — the counters are never truly still.
#
# Settings live in ugreen-leds-mon.conf next to this script (the installer
# creates it from ugreen-leds-mon.conf.example and never overwrites it).

DIR="$(cd "$(dirname "$0")" && pwd)"
CLI="${UGREEN_LEDS_CLI:-/usr/local/bin/ugreen_leds_cli}"
[ -x "$CLI" ] || CLI="$DIR/ugreen_leds_cli"

INTERVAL=2
DISKS=""
NICS=""
EXCLUDE_DISKS=""
SKIP_BOOT_DISK=1

DISK_ACTIVITY=1
NET_ACTIVITY=1
DISK_THRESHOLD_KB=128
NET_THRESHOLD_KB=64
IDLE_HOLD=2

COLOR_DISK_HEALTH="255 255 255"
COLOR_DISK_ACTIVE=""
COLOR_DISK_EMPTY=""
BRIGHTNESS_DISK_LEDS=64
COLOR_NETDEV_NORMAL="255 255 255"
COLOR_NETDEV_ACTIVE=""
BRIGHTNESS_NETDEV_LED=96
BLINK_ON=300
BLINK_OFF=200

[ -f "$DIR/ugreen-leds-mon.conf" ] && . "$DIR/ugreen-leds-mon.conf"

# the active colors default to the idle ones (blink, same color)
[ -n "$COLOR_DISK_ACTIVE" ] || COLOR_DISK_ACTIVE="$COLOR_DISK_HEALTH"
[ -n "$COLOR_NETDEV_ACTIVE" ] || COLOR_NETDEV_ACTIVE="$COLOR_NETDEV_NORMAL"

UGREEN_MODEL=idx6011
export UGREEN_MODEL

# The panel reads this pid file to stop the monitor when the LEDs are
# switched off, so clean it up rather than leaving a stale pid behind.
PIDFILE="${UGREEN_LEDS_PIDFILE:-/var/run/ugreen-leds-mon.pid}"
[ -f "$PIDFILE" ] && kill "$(cat "$PIDFILE")" 2>/dev/null
echo $$ > "$PIDFILE"
# INT/TERM must exit explicitly: sh runs the handler only after the current
# sleep returns, and a handler that just falls through would resume the loop
# — the monitor would then survive the panel's "LEDs off".
trap 'rm -f "$PIDFILE"; exit 0' INT TERM
trap 'rm -f "$PIDFILE"' EXIT

# Map a device node to the whole disk backing it (partition -> parent).
dev_to_disk() {
    n=$(lsblk -no PKNAME "$1" 2>/dev/null | head -1)
    [ -n "$n" ] || n=$(lsblk -no KNAME "$1" 2>/dev/null | head -1)
    echo "$n"
}

# Disks holding the OS. They are never idle (logs, SQLite, ZFS commits), so
# leaving them in the bay list means one LED blinks forever.
boot_disks() {
    for mp in / /boot /boot/efi; do
        src=$(findmnt -no SOURCE -T "$mp" 2>/dev/null | head -1)
        case "$src" in
            /dev/*) dev_to_disk "${src%%\[*}" ;;
        esac
    done
    if command -v zpool >/dev/null 2>&1; then
        zpool status -LP boot-pool 2>/dev/null | awk '$1 ~ /^\/dev\// {print $1}' |
        while read -r d; do dev_to_disk "$d"; done
    fi
}

if [ -z "$DISKS" ]; then
    skip=" $EXCLUDE_DISKS "
    if [ "$SKIP_BOOT_DISK" = 1 ]; then
        for b in $(boot_disks); do skip="$skip$b "; done
    fi
    all_sd=$(ls -1 /sys/block 2>/dev/null | grep '^sd' | sort)
    DISKS=""
    dropped=""
    for d in $all_sd; do
        case "$skip" in
            *" $d "*) dropped="$dropped$d "; continue ;;
        esac
        DISKS="$DISKS$d "
        [ "$(echo "$DISKS" | wc -w)" -ge 6 ] && break
    done
    # Excluding everything would leave every bay dark and look like a
    # broken install — better to watch the OS disk than nothing at all.
    if [ -z "$DISKS" ] && [ -n "$all_sd" ]; then
        echo "ugreen-leds-mon: every disk was excluded, falling back to all of them"
        DISKS="$(echo "$all_sd" | head -6 | tr '\n' ' ')"
        dropped=""
    fi
    [ -n "$dropped" ] && echo "ugreen-leds-mon: not watching: $dropped(OS disk / EXCLUDE_DISKS; set SKIP_BOOT_DISK=0 to keep it)"
fi
if [ -z "$NICS" ]; then
    NICS="$(for n in /sys/class/net/*; do
                [ -e "$n/device" ] && basename "$n"
            done | sort | head -2 | tr '\n' ' ')"
fi
echo "ugreen-leds-mon: disks: ${DISKS:-none}  nics: ${NICS:-none}  interval: ${INTERVAL}s"
echo "ugreen-leds-mon: thresholds: disk ${DISK_THRESHOLD_KB} kB, net ${NET_THRESHOLD_KB} kB per poll, idle hold ${IDLE_HOLD}"
[ "$DISK_ACTIVITY" = 1 ] || echo "ugreen-leds-mon: disk activity blinking disabled"
[ "$NET_ACTIVITY" = 1 ] || echo "ugreen-leds-mon: network activity blinking disabled"

# /proc/diskstats fields 6 and 10 are sectors read/written (512 B each).
# One awk pass for all bays — this runs every INTERVAL forever.
disk_io_all() {
    awk -v list="$DISKS" '
        BEGIN { n = split(list, d, " ") }
        { for (i = 1; i <= n; i++) if ($3 == d[i]) v[i] = $6 + $10 }
        END { for (i = 1; i <= n; i++) printf "%s ", (i in v) ? v[i] : 0 }
    ' /proc/diskstats
}

nic_io() {
    r=$(cat "/sys/class/net/$1/statistics/rx_bytes" 2>/dev/null || echo 0)
    t=$(cat "/sys/class/net/$1/statistics/tx_bytes" 2>/dev/null || echo 0)
    echo $((r + t))
}

# $2 is an "R G B" triplet and must word-split into three CLI arguments.
set_blink() { "$CLI" "$1" -color $2 -blink "$BLINK_ON" "$BLINK_OFF" >/dev/null 2>&1; }
set_solid() { "$CLI" "$1" -on -color $2 -brightness "$3" >/dev/null 2>&1; }

# Decide whether an LED should blink, with hysteresis: activity switches it
# on at once, but it takes IDLE_HOLD consecutive quiet polls to go solid
# again, so a burst does not flicker the LED between the two states.
# args: led  delta_kB  threshold_kB  state_var  idle_var  color_active
#       color_idle  brightness
update_led() {
    _led=$1; _kb=$2; _thr=$3; _sv=$4; _iv=$5; _cact=$6; _cidle=$7; _br=$8
    eval "_st=\$$_sv"
    eval "_idle=\$$_iv"
    if [ "$_kb" -ge "$_thr" ]; then
        _idle=0
        _want=1
    else
        _idle=$((_idle + 1))
        if [ "$_st" = 1 ] && [ "$_idle" -lt "$IDLE_HOLD" ]; then
            _want=1
        else
            _want=0
        fi
    fi
    if [ "$_want" != "$_st" ]; then
        if [ "$_want" = 1 ]; then
            set_blink "$_led" "$_cact"
        else
            set_solid "$_led" "$_cidle" "$_br"
        fi
        eval "$_sv=$_want"
    fi
    eval "$_iv=$_idle"
}

# Bays with nothing behind them: dark by default, so a half-populated NAS
# does not glow for empty slots (start.sh lights all six to stop the boot
# animation before the disks are known). COLOR_DISK_EMPTY lights them anyway.
n=$(echo "$DISKS" | wc -w)
i=$((n + 1))
while [ "$i" -le 6 ]; do
    if [ -n "$COLOR_DISK_EMPTY" ]; then
        set_solid "disk$i" "$COLOR_DISK_EMPTY" "$BRIGHTNESS_DISK_LEDS"
    else
        "$CLI" "disk$i" -off >/dev/null 2>&1
    fi
    i=$((i + 1))
done

# prime counters; state -1 forces an initial solid write per LED
i=1
set -- $(disk_io_all)
for d in $DISKS; do
    eval "prev_d$i=${1:-0}"
    eval "st_d$i=-1"
    eval "idle_d$i=0"
    shift 2>/dev/null || true
    i=$((i + 1))
done
i=1
for n in $NICS; do
    eval "prev_n$i=$(nic_io "$n")"
    eval "st_n$i=-1"
    eval "idle_n$i=0"
    i=$((i + 1))
done

while :; do
    i=1
    set -- $(disk_io_all)
    for d in $DISKS; do
        cur=${1:-0}
        shift 2>/dev/null || true
        eval "prev=\$prev_d$i"
        delta=$((cur - prev))
        [ "$delta" -lt 0 ] && delta=0          # counter reset
        if [ "$DISK_ACTIVITY" = 1 ]; then
            update_led "disk$i" $((delta / 2)) "$DISK_THRESHOLD_KB" \
                "st_d$i" "idle_d$i" "$COLOR_DISK_ACTIVE" \
                "$COLOR_DISK_HEALTH" "$BRIGHTNESS_DISK_LEDS"
        else
            eval "st=\$st_d$i"
            if [ "$st" != 0 ]; then
                set_solid "disk$i" "$COLOR_DISK_HEALTH" "$BRIGHTNESS_DISK_LEDS"
                eval "st_d$i=0"
            fi
        fi
        eval "prev_d$i=$cur"
        i=$((i + 1))
    done

    i=1
    for n in $NICS; do
        led=netdev
        [ "$i" = 2 ] && led=netdev2
        cur=$(nic_io "$n")
        eval "prev=\$prev_n$i"
        delta=$((cur - prev))
        [ "$delta" -lt 0 ] && delta=0
        if [ "$NET_ACTIVITY" = 1 ]; then
            update_led "$led" $((delta / 1024)) "$NET_THRESHOLD_KB" \
                "st_n$i" "idle_n$i" "$COLOR_NETDEV_ACTIVE" \
                "$COLOR_NETDEV_NORMAL" "$BRIGHTNESS_NETDEV_LED"
        else
            eval "st=\$st_n$i"
            if [ "$st" != 0 ]; then
                set_solid "$led" "$COLOR_NETDEV_NORMAL" "$BRIGHTNESS_NETDEV_LED"
                eval "st_n$i=0"
            fi
        fi
        eval "prev_n$i=$cur"
        i=$((i + 1))
    done

    sleep "$INTERVAL"
done
