#!/bin/sh
# ugreen-leds-mon — userspace activity monitor for the UGREEN iDX6011 (Pro)
# front LEDs. No kernel module needed: disk and network activity is read
# from /proc//sys counters, and the LED MCU's own hardware blink mode does
# the animation. Only state TRANSITIONS touch the i2c bus, so steady state
# costs nothing but a few file reads per poll.
#
# Activity needs to cross a THRESHOLD before an LED starts blinking. Without
# one a single ARP packet or a ZFS metadata commit is enough to keep an idle
# NAS blinking forever — the counters are never truly still.
#
# A failing disk turns its LED red (COLOR_DISK_FAIL). The monitor never asks
# the disks for that itself: every SMART query is a command to the drive,
# which on many drives resets the spin-down timer or unparks the heads. It
# reuses what the system already knows instead — the ZFS pool state (kernel
# state, no disk access) and, on TrueNAS, the SMART alerts TrueNAS raises
# from its own scheduled checks.
#
# Settings live in ugreen-leds-mon.conf next to this script (the installer
# creates it from ugreen-leds-mon.conf.example and never overwrites it).

#
# Besides running as the monitor it answers two questions and exits:
#   sh ugreen-leds-mon.sh --show-map      which disk sits behind which bay LED
#   sh ugreen-leds-mon.sh --identify N    blink bay N blue for 10 s

DIR="$(cd "$(dirname "$0")" && pwd)"
CLI="${UGREEN_LEDS_CLI:-/usr/local/bin/ugreen_leds_cli}"
[ -x "$CLI" ] || CLI="$DIR/ugreen_leds_cli"

MODE=run
case "${1:-}" in
    "")          ;;
    --show-map)  MODE=show-map ;;
    --identify)  MODE=identify; IDENT_BAY="${2:-}" ;;
    *)           echo "usage: $0 [--show-map | --identify <bay 1-6>]" >&2; exit 2 ;;
esac
if [ "$MODE" = identify ]; then
    case "$IDENT_BAY" in
        [1-6]) ;;
        *) echo "--identify needs a bay number from 1 to 6" >&2; exit 2 ;;
    esac
fi

# The panel reads this pid file to stop the monitor when the LEDs are
# switched off, so clean it up rather than leaving a stale pid behind. It is
# written first thing: a monitor stopped while it is still starting up must
# not slip through and keep running.
if [ "$MODE" = run ]; then
    PIDFILE="${UGREEN_LEDS_PIDFILE:-/var/run/ugreen-leds-mon.pid}"
    # (after a re-exec on a disk change the pid file already holds our own pid)
    if [ -f "$PIDFILE" ] && [ "$(cat "$PIDFILE")" != "$$" ]; then
        kill "$(cat "$PIDFILE")" 2>/dev/null
    fi
    echo $$ > "$PIDFILE"
    # INT/TERM must exit explicitly: sh runs the handler only after the
    # current sleep returns, and a handler that just falls through would
    # resume the loop — the monitor would then survive the panel's "LEDs off".
    trap 'rm -f "$PIDFILE"; exit 0' INT TERM
    trap 'rm -f "$PIDFILE"' EXIT
fi

INTERVAL=2
LED_LAYOUT="${LED_LAYOUT:-auto}"            # start.sh passes its verdict down
DISKS=""
BAY_ATA=""
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
NETDEV_COLOR_BY_SPEED=0
COLOR_NIC_10G="0 255 0"
COLOR_NIC_5G="255 0 255"
COLOR_NIC_2G5="255 90 0"
COLOR_NIC_1G="255 200 0"
COLOR_NIC_OTHER="255 255 255"
BLINK_ON=300
BLINK_OFF=200

DISK_HEALTH_CHECK=1
COLOR_DISK_FAIL="255 0 0"
HEALTH_INTERVAL=60

[ -f "$DIR/ugreen-leds-mon.conf" ] && . "$DIR/ugreen-leds-mon.conf"
AUTO_DISKS=0
[ -z "$DISKS" ] && AUTO_DISKS=1

# the active colors default to the idle ones (blink, same color)
[ -n "$COLOR_DISK_ACTIVE" ] || COLOR_DISK_ACTIVE="$COLOR_DISK_HEALTH"
[ -n "$COLOR_NETDEV_ACTIVE" ] || COLOR_NETDEV_ACTIVE="$COLOR_NETDEV_NORMAL"

UGREEN_MODEL=idx6011
export UGREEN_MODEL
SYS_BLOCK="${UGREEN_SYS_BLOCK:-/sys/block}"
SYS_PCI="${UGREEN_SYS_PCI:-/sys/bus/pci/devices}"
SYS_NET="${UGREEN_SYS_NET:-/sys/class/net}"
PRODUCT="$(cat /sys/class/dmi/id/product_name 2>/dev/null)"

# The CLI names the LEDs after the Pro's front: power, netdev, netdev2,
# disk1..disk6. The non-Pro has the same nine MCU registers but only ONE LAN
# LED, so its bays start one register earlier: "netdev2" is bay 1, disk1..5
# are bays 2..6 and disk6 is not wired to anything.
if [ "$LED_LAYOUT" = auto ]; then
    case "$PRODUCT" in
        *iDX6011*Pro*) LED_LAYOUT=pro ;;
        *iDX6011*)     LED_LAYOUT=nonpro ;;
        *)             LED_LAYOUT=pro ;;
    esac
fi

# Which SATA port feeds which bay. The bays are wired to two controllers —
# an ASMedia ASM1164 (4 ports) and the Intel chipset (2 ports) — differently
# on the two models:
#   Pro      ASMedia ports -> bays 1 2 5 6   Intel ports -> bays 3 4
#   non-Pro  ASMedia ports -> bays 1 2 3 4   Intel ports -> bays 5 6
# The controllers are found by PCI vendor and their ports taken in order,
# never by PCI address or a fixed ataN list: the ASMedia's bus address moves
# between boots and BIOS versions. (Pro order from noodlemctwoodle's Unraid
# plugin; non-Pro measured by aoreskovic and omgapuppy.) BAY_ATA overrides it.
# Prints one "<ataN> <bay>" line per known port.
bay_ports() {
    if [ -n "$BAY_ATA" ]; then
        _b=1
        for _p in $BAY_ATA; do echo "$_p $_b"; _b=$((_b + 1)); done
        return
    fi
    if [ "$LED_LAYOUT" = nonpro ]; then
        _asm="1 2 3 4"; _intel="5 6"
    else
        _asm="1 2 5 6"; _intel="3 4"
    fi
    _seen=""
    for _dev in "$SYS_PCI"/*; do
        _ports=$(ls -1 "$_dev" 2>/dev/null | sed -n 's/^ata\([0-9][0-9]*\)$/\1/p' | sort -n | sed 's/^/ata/')
        [ -n "$_ports" ] || continue
        case "$(cat "$_dev/vendor" 2>/dev/null)" in
            0x1b21) _v=asm;   set -- $_asm ;;
            0x8086) _v=intel; set -- $_intel ;;
            *) continue ;;
        esac
        case "$_seen" in *" $_v "*) continue ;; esac   # first controller of each kind only
        _seen="$_seen $_v "
        for _p in $_ports; do
            [ $# -gt 0 ] || break
            echo "$_p $1"
            shift
        done
    done
}

bay_led() {
    if [ "$LED_LAYOUT" = nonpro ]; then
        if [ "$1" = 1 ]; then echo netdev2; else echo "disk$(($1 - 1))"; fi
    else
        echo "disk$1"
    fi
}

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

# The SATA port a disk hangs off, from its sysfs path (.../ata3/host2/...).
disk_ata() {
    readlink "$SYS_BLOCK/$1" 2>/dev/null | sed -n 's|.*/\(ata[0-9]*\)/.*|\1|p'
}

# DISKS lists the bays in order, "-" for an empty one.
if [ -z "$DISKS" ]; then
    skip=" $EXCLUDE_DISKS "
    if [ "$SKIP_BOOT_DISK" = 1 ]; then
        for b in $(boot_disks); do skip="$skip$b "; done
    fi
    all_sd=$(ls -1 "$SYS_BLOCK" 2>/dev/null | grep '^sd' | sort)
    candidates=""
    dropped=""
    for d in $all_sd; do
        case "$skip" in
            *" $d "*) dropped="$dropped$d "; continue ;;
        esac
        candidates="$candidates$d "
    done
    DISKS=""
    # With the bay's SATA port known, a disk lands on the LED of the bay it
    # actually sits in — and a gap stays a gap instead of shifting the rest.
    BAY_MAP=$(bay_ports)
    if [ -n "$BAY_MAP" ] && [ -n "$candidates" ]; then
        placed=""
        for b in 1 2 3 4 5 6; do
            port=$(echo "$BAY_MAP" | awk -v b="$b" '$2 == b { print $1; exit }')
            hit=-
            for d in $candidates; do
                [ -n "$port" ] || break
                if [ "$(disk_ata "$d")" = "$port" ]; then
                    hit=$d
                    placed="$placed$d "
                    break
                fi
            done
            DISKS="$DISKS$hit "
        done
        if [ -z "$placed" ]; then
            echo "ugreen-leds-mon: no disk on the bay ports, using sorted order"
            DISKS=""
        else
            off_bay=""
            for d in $candidates; do
                case " $placed " in *" $d "*) ;; *) off_bay="$off_bay$d " ;; esac
            done
            [ -n "$off_bay" ] && echo "ugreen-leds-mon: not in a bay: $off_bay"
        fi
    fi
    if [ -z "$DISKS" ]; then
        for d in $candidates; do
            DISKS="$DISKS$d "
            [ "$(echo "$DISKS" | wc -w)" -ge 6 ] && break
        done
    fi
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
    NICS="$(for n in "$SYS_NET"/*; do
                [ -e "$n/device" ] && basename "$n"
            done | sort | head -2 | tr '\n' ' ')"
fi
# One LED per NIC on the Pro; the non-Pro's single LAN LED shows both ports.
if [ "$LED_LAYOUT" = nonpro ]; then
    NET_GROUPS=$(echo $NICS | tr ' ' ',')
else
    NET_GROUPS="$NICS"
fi
if [ "$MODE" = run ]; then
    echo "ugreen-leds-mon: layout: $LED_LAYOUT"
    echo "ugreen-leds-mon: disks: ${DISKS:-none}  nics: ${NICS:-none}  interval: ${INTERVAL}s"
    echo "ugreen-leds-mon: thresholds: disk ${DISK_THRESHOLD_KB} kB, net ${NET_THRESHOLD_KB} kB per poll, idle hold ${IDLE_HOLD}"
    [ "$DISK_ACTIVITY" = 1 ] || echo "ugreen-leds-mon: disk activity blinking disabled"
    [ "$NET_ACTIVITY" = 1 ] || echo "ugreen-leds-mon: network activity blinking disabled"
fi

# /proc/diskstats fields 6 and 10 are sectors read/written (512 B each).
# One awk pass for all bays — this runs every INTERVAL forever.
disk_io_all() {
    awk -v list="$DISKS" '
        BEGIN { n = split(list, d, " ") }
        { for (i = 1; i <= n; i++) if ($3 == d[i]) v[i] = $6 + $10 }
        END { for (i = 1; i <= n; i++) printf "%s ", (i in v) ? v[i] : 0 }
    ' /proc/diskstats
}

# $1 is one NIC, or several joined by commas that share an LED.
nic_io() {
    _sum=0
    for _n in $(echo "$1" | tr ',' ' '); do
        r=$(cat "$SYS_NET/$_n/statistics/rx_bytes" 2>/dev/null || echo 0)
        t=$(cat "$SYS_NET/$_n/statistics/tx_bytes" 2>/dev/null || echo 0)
        _sum=$((_sum + r + t))
    done
    echo "$_sum"
}

# NETDEV_COLOR_BY_SPEED=1: the LAN LED shows the negotiated link speed, so a
# port that fell back to 1G stands out (idea and colors by just-apaz, #9).
# $1 as for nic_io; a shared LED shows its fastest port. Sets _ncol to an
# "R G B" triplet, or "off" when no port has a link. Builtins only (every poll).
speed_color() {
    _best=-1
    _ifs=$IFS; IFS=,
    for _n in $1; do
        _op=""; { read -r _op < "$SYS_NET/$_n/operstate"; } 2>/dev/null
        [ "$_op" = up ] || continue
        _sp=""; { read -r _sp < "$SYS_NET/$_n/speed"; } 2>/dev/null
        case "$_sp" in ''|*[!0-9]*) _sp=0 ;; esac
        [ "$_sp" -gt "$_best" ] && _best=$_sp
    done
    IFS=$_ifs
    case "$_best" in
        -1)    _ncol=off ;;
        10000) _ncol=$COLOR_NIC_10G ;;
        5000)  _ncol=$COLOR_NIC_5G ;;
        2500)  _ncol=$COLOR_NIC_2G5 ;;
        1000)  _ncol=$COLOR_NIC_1G ;;
        *)     _ncol=$COLOR_NIC_OTHER ;;
    esac
}

# $2 is an "R G B" triplet and must word-split into three CLI arguments.
set_blink() { "$CLI" "$1" -color $2 -blink "$BLINK_ON" "$BLINK_OFF" >/dev/null 2>&1; }
set_solid() { "$CLI" "$1" -on -color $2 -brightness "$3" >/dev/null 2>&1; }

bay_disk() { echo $DISKS - - - - - - | cut -d' ' -f"$1"; }

if [ "$MODE" = show-map ]; then
    [ -n "${BAY_MAP+x}" ] || BAY_MAP=$(bay_ports)
    echo "model: ${PRODUCT:-unknown}   layout: $LED_LAYOUT"
    printf '%-4s %-8s %-7s %-6s %s\n' bay led port disk "model / serial"
    for b in 1 2 3 4 5 6; do
        d=$(bay_disk "$b")
        port=$(echo "$BAY_MAP" | awk -v b="$b" '$2 == b { print $1; exit }')
        if [ "$d" != - ]; then
            [ -n "$port" ] || port=$(disk_ata "$d")
            info=$(lsblk -dno MODEL,SERIAL "/dev/$d" 2>/dev/null | tr -s ' ' | sed 's/ *$//; s/ \([^ ]*\)$/ \/ \1/')
        else
            info="(empty)"
        fi
        printf '%-4s %-8s %-7s %-6s %s\n' "$b" "$(bay_led "$b")" "${port:--}" "$d" "$info"
    done
    exit 0
fi

# Light one bay so its position can be checked from the front. A running
# monitor only rewrites an LED when its state changes, so the bay is put back
# in its idle color here rather than waiting for the monitor to do it.
if [ "$MODE" = identify ]; then
    led=$(bay_led "$IDENT_BAY")
    identify_restore() {
        if [ "$(bay_disk "$IDENT_BAY")" != - ]; then
            set_solid "$led" "$COLOR_DISK_HEALTH" "$BRIGHTNESS_DISK_LEDS"
        elif [ -n "$COLOR_DISK_EMPTY" ]; then
            set_solid "$led" "$COLOR_DISK_EMPTY" "$BRIGHTNESS_DISK_LEDS"
        else
            "$CLI" "$led" -off >/dev/null 2>&1
        fi
    }
    # Ctrl+C (or a kill) must not leave the bay blinking blue
    trap 'identify_restore; exit 130' INT TERM
    echo "blinking bay $IDENT_BAY ($led, disk: $(bay_disk "$IDENT_BAY")) blue for 10 s"
    "$CLI" "$led" -color 0 0 255 -blink 150 150 >/dev/null 2>&1
    sleep "${UGREEN_IDENTIFY_SECS:-10}"
    identify_restore
    exit 0
fi

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

# ---- disk health, from state the system already keeps ----
# Prints "<disk> <reason>" for every disk with a known problem. Neither source
# sends anything to a disk.

# ZFS: a leaf device that is not ONLINE (FAULTED, UNAVAIL, REMOVED, DEGRADED,
# OFFLINE), or one with read/write/checksum errors. Spares show AVAIL/INUSE.
zfs_problems() {
    command -v zpool >/dev/null 2>&1 || return 0
    zpool status -LP 2>/dev/null | awk '
        $1 ~ /^\/dev\// {
            bad = ($2 != "ONLINE" && $2 != "AVAIL" && $2 != "INUSE")
            for (f = 3; f <= 5; f++) if ($f != "" && $f != "0") bad = 1
            if (bad) print $1, "zfs:" $2 "(" $3 "/" $4 "/" $5 ")"
        }' |
    while read -r dev why; do
        d=$(dev_to_disk "$dev")
        [ -n "$d" ] && echo "$d $why"
    done
}

# TrueNAS: its SMART alert source checks every disk itself (every 90 min in
# 25.10) and raises SMARTUncorrectedErrors / SMARTFailedSelfTest / ... with the
# disk name in args.name. Dismissed alerts still count — dismissing means "I
# have seen it", not "the disk is fine"; the alert disappears on its own once
# TrueNAS no longer finds the problem.
smart_alert_problems() {
    command -v midclt >/dev/null 2>&1 || return 0
    command -v python3 >/dev/null 2>&1 || return 0
    midclt call alert.list 2>/dev/null | python3 -c '
import json, sys
try:
    alerts = json.load(sys.stdin)
except Exception:
    sys.exit(0)
for a in alerts if isinstance(alerts, list) else []:
    klass = str(a.get("klass") or "")
    args = a.get("args")
    if klass.startswith("SMART") and isinstance(args, dict) and args.get("name"):
        print(str(args["name"]).replace("/dev/", ""), "smart:" + klass)
' 2>/dev/null
}

# Bays with nothing behind them: dark by default, so a half-populated NAS
# does not glow for empty slots (start.sh lights all six to stop the boot
# animation before the disks are known). COLOR_DISK_EMPTY lights them anyway.
i=1
for d in $DISKS - - - - - -; do             # padded to six bays
    [ "$i" -le 6 ] || break
    eval "led_d$i=$(bay_led "$i")"
    if [ "$d" = - ]; then
        eval "led=\$led_d$i"
        if [ -n "$COLOR_DISK_EMPTY" ]; then
            set_solid "$led" "$COLOR_DISK_EMPTY" "$BRIGHTNESS_DISK_LEDS"
        else
            "$CLI" "$led" -off >/dev/null 2>&1
        fi
    fi
    i=$((i + 1))
done
# the non-Pro's disk6 register has no LED behind it
[ "$LED_LAYOUT" = nonpro ] && "$CLI" disk6 -off >/dev/null 2>&1

# Re-read the health sources and mark each bay. A bay whose verdict changes
# gets state -1, so the next pass rewrites its LED in the new color. midclt
# starts a Python process, so the TrueNAS alerts are read only every 5th check
# — TrueNAS itself refreshes them every 90 minutes anyway.
smart_list=""
health_round=0
check_health() {
    [ "$DISK_HEALTH_CHECK" = 1 ] || return 0
    if [ $((health_round % 5)) = 0 ]; then
        smart_list=$(smart_alert_problems)
    fi
    health_round=$((health_round + 1))
    problems=$(zfs_problems)
    [ -n "$smart_list" ] && problems="$problems
$smart_list"
    i=1
    for d in $DISKS; do
        [ "$d" = - ] && { i=$((i + 1)); continue; }
        why=$(echo "$problems" | awk -v d="$d" '$1 == d { printf "%s ", $2 }')
        new=0
        [ -n "$why" ] && new=1
        eval "old=\$bad_d$i"
        if [ "$new" != "$old" ]; then
            eval "bad_d$i=$new"
            eval "st_d$i=-1"
            if [ "$new" = 1 ]; then
                echo "ugreen-leds-mon: $d (bay $i) needs attention: $why— LED red"
            else
                echo "ugreen-leds-mon: $d (bay $i) healthy again"
            fi
        fi
        i=$((i + 1))
    done
}

# prime counters; state -1 forces an initial solid write per LED
i=1
set -- $(disk_io_all)
for d in $DISKS; do
    eval "prev_d$i=${1:-0}"
    eval "st_d$i=-1"
    eval "idle_d$i=0"
    eval "bad_d$i=0"
    shift 2>/dev/null || true
    i=$((i + 1))
done
i=1
for n in $NET_GROUPS; do
    eval "prev_n$i=$(nic_io "$n")"
    eval "st_n$i=-1"
    eval "idle_n$i=0"
    i=$((i + 1))
done

if [ "$DISK_HEALTH_CHECK" = 1 ]; then
    echo "ugreen-leds-mon: disk health: zfs$(command -v midclt >/dev/null 2>&1 && echo ' + TrueNAS SMART alerts'), every ${HEALTH_INTERVAL}s"
    check_health
fi
health_wait=0

# The bay map is built once, from the disks present at start. A pulled,
# swapped or re-inserted drive changes the set of sd* devices (and often its
# own name), so start over with a fresh map when that happens. Builtins only:
# this runs on every poll.
sd_signature() {
    _sig=""
    for _b in "$SYS_BLOCK"/sd*; do
        { [ -e "$_b" ] || [ -L "$_b" ]; } && _sig="$_sig ${_b##*/}"
    done
}
sd_signature
sd_start="$_sig"

while :; do
    if [ "$AUTO_DISKS" = 1 ]; then
        sd_signature
        if [ "$_sig" != "$sd_start" ]; then
            echo "ugreen-leds-mon: disks changed (${sd_start# } -> ${_sig# }), re-reading the bays"
            exec sh "$0"
        fi
    fi

    i=1
    set -- $(disk_io_all)
    for d in $DISKS; do
        cur=${1:-0}
        shift 2>/dev/null || true
        [ "$d" = - ] && { i=$((i + 1)); continue; }
        eval "led=\$led_d$i"
        eval "prev=\$prev_d$i"
        delta=$((cur - prev))
        [ "$delta" -lt 0 ] && delta=0          # counter reset
        eval "bad=\$bad_d$i"
        if [ "$bad" = 1 ]; then
            c_act="$COLOR_DISK_FAIL"; c_idle="$COLOR_DISK_FAIL"   # still blinks on activity, in red
        else
            c_act="$COLOR_DISK_ACTIVE"; c_idle="$COLOR_DISK_HEALTH"
        fi
        if [ "$DISK_ACTIVITY" = 1 ]; then
            update_led "$led" $((delta / 2)) "$DISK_THRESHOLD_KB" \
                "st_d$i" "idle_d$i" "$c_act" \
                "$c_idle" "$BRIGHTNESS_DISK_LEDS"
        else
            eval "st=\$st_d$i"
            if [ "$st" != 0 ]; then
                set_solid "$led" "$c_idle" "$BRIGHTNESS_DISK_LEDS"
                eval "st_d$i=0"
            fi
        fi
        eval "prev_d$i=$cur"
        i=$((i + 1))
    done

    i=1
    for n in $NET_GROUPS; do
        led=netdev
        [ "$i" = 2 ] && led=netdev2
        cur=$(nic_io "$n")
        eval "prev=\$prev_n$i"
        delta=$((cur - prev))
        [ "$delta" -lt 0 ] && delta=0
        n_norm="$COLOR_NETDEV_NORMAL"; n_act="$COLOR_NETDEV_ACTIVE"
        if [ "$NETDEV_COLOR_BY_SPEED" = 1 ]; then
            speed_color "$n"
            eval "oldcol=\$col_n$i"
            if [ "$_ncol" != "$oldcol" ]; then
                eval "col_n$i=\"\$_ncol\""
                eval "st_n$i=-1"                 # rewrite the LED in the new color
                [ "$_ncol" = off ] && "$CLI" "$led" -off >/dev/null 2>&1
            fi
            if [ "$_ncol" = off ]; then           # no link: stays dark
                eval "prev_n$i=$cur"
                i=$((i + 1))
                continue
            fi
            n_norm="$_ncol"; n_act="$_ncol"
        fi
        if [ "$NET_ACTIVITY" = 1 ]; then
            update_led "$led" $((delta / 1024)) "$NET_THRESHOLD_KB" \
                "st_n$i" "idle_n$i" "$n_act" \
                "$n_norm" "$BRIGHTNESS_NETDEV_LED"
        else
            eval "st=\$st_n$i"
            if [ "$st" != 0 ]; then
                set_solid "$led" "$n_norm" "$BRIGHTNESS_NETDEV_LED"
                eval "st_n$i=0"
            fi
        fi
        eval "prev_n$i=$cur"
        i=$((i + 1))
    done

    if [ "$DISK_HEALTH_CHECK" = 1 ]; then
        health_wait=$((health_wait + INTERVAL))
        if [ "$health_wait" -ge "$HEALTH_INTERVAL" ]; then
            health_wait=0
            check_health
        fi
    fi

    sleep "$INTERVAL"
done
