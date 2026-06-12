#!/bin/sh
# ugreen-leds-mon — userspace activity monitor for the UGREEN iDX6011 Pro
# front LEDs. No kernel module needed: disk and network activity is read
# from /proc//sys counters, and the LED MCU's own hardware blink mode does
# the animation. Only state TRANSITIONS touch the i2c bus, so steady state
# costs nothing but a few file reads per poll.
#
# Optional config file next to this script (ugreen-leds-mon.conf):
#   INTERVAL=2            poll seconds
#   DISKS="sda sdb ..."   bay order disk1..disk6 (default: /sys/block sd*,
#                         sorted — adjust if your bay order differs)
#   NICS="enp2s0 ..."     LAN1/LAN2 order (default: physical NICs, sorted)
#   BRIGHT_DISK=64        idle disk brightness
#   BRIGHT_NET=96         idle network brightness

DIR="$(cd "$(dirname "$0")" && pwd)"
CLI="${UGREEN_LEDS_CLI:-/usr/local/bin/ugreen_leds_cli}"
[ -x "$CLI" ] || CLI="$DIR/ugreen_leds_cli"

INTERVAL=2
DISKS=""
NICS=""
BRIGHT_DISK=64
BRIGHT_NET=96
[ -f "$DIR/ugreen-leds-mon.conf" ] && . "$DIR/ugreen-leds-mon.conf"

UGREEN_MODEL=idx6011
export UGREEN_MODEL

PIDFILE="${UGREEN_LEDS_PIDFILE:-/var/run/ugreen-leds-mon.pid}"
[ -f "$PIDFILE" ] && kill "$(cat "$PIDFILE")" 2>/dev/null
echo $$ > "$PIDFILE"

if [ -z "$DISKS" ]; then
    DISKS="$(ls -1 /sys/block 2>/dev/null | grep '^sd' | sort | head -6 | tr '\n' ' ')"
fi
if [ -z "$NICS" ]; then
    NICS="$(for n in /sys/class/net/*; do
                [ -e "$n/device" ] && basename "$n"
            done | sort | head -2 | tr '\n' ' ')"
fi
echo "ugreen-leds-mon: disks: ${DISKS:-none}  nics: ${NICS:-none}  interval: ${INTERVAL}s"

disk_io() {
    awk -v d="$1" '$3==d {print $6+$10; f=1} END {if (!f) print 0}' /proc/diskstats
}

nic_io() {
    r=$(cat "/sys/class/net/$1/statistics/rx_bytes" 2>/dev/null || echo 0)
    t=$(cat "/sys/class/net/$1/statistics/tx_bytes" 2>/dev/null || echo 0)
    echo $((r + t))
}

set_blink() { "$CLI" "$1" -color 255 255 255 -blink 300 200 >/dev/null 2>&1; }
set_solid() { "$CLI" "$1" -on -color 255 255 255 -brightness "$2" >/dev/null 2>&1; }

# prime counters; state -1 forces an initial solid write per LED
i=1
for d in $DISKS; do
    eval "prev_d$i=\$(disk_io "$d")"
    eval "st_d$i=-1"
    i=$((i + 1))
done
i=1
for n in $NICS; do
    eval "prev_n$i=\$(nic_io "$n")"
    eval "st_n$i=-1"
    i=$((i + 1))
done

while :; do
    i=1
    for d in $DISKS; do
        cur=$(disk_io "$d")
        eval "prev=\$prev_d$i"
        eval "st=\$st_d$i"
        if [ "$cur" != "$prev" ]; then want=1; else want=0; fi
        if [ "$want" != "$st" ]; then
            if [ "$want" = 1 ]; then
                set_blink "disk$i"
            else
                set_solid "disk$i" "$BRIGHT_DISK"
            fi
            eval "st_d$i=$want"
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
        eval "st=\$st_n$i"
        if [ "$cur" != "$prev" ]; then want=1; else want=0; fi
        if [ "$want" != "$st" ]; then
            if [ "$want" = 1 ]; then
                set_blink "$led"
            else
                set_solid "$led" "$BRIGHT_NET"
            fi
            eval "st_n$i=$want"
        fi
        eval "prev_n$i=$cur"
        i=$((i + 1))
    done

    sleep "$INTERVAL"
done
