#!/bin/bash
# Functional test for the front-LED package: stubbed CLI, real /proc
# counters, run under dash (TrueNAS post-init sh). Covers the monitor's
# initial solid writes, the activity threshold, blink on a real burst, the
# idle hold, custom colors, the activity kill switch, boot-disk exclusion,
# and that start.sh picks its colors up from the same config.
set -u
T=/tmp/ledmon-test
SRC="$(cd "$(dirname "$0")/.." && pwd)/packaging/leds/ugreen-leds-mon.sh"
[ -f "$SRC" ] || { echo "not found: $SRC"; exit 1; }

FAILED=0
pass() { echo "  PASS  $1"; }
fail() { echo "  FAIL  $1"; FAILED=$((FAILED + 1)); }
check()     { if grep -qE -- "$1" "$T/calls.log" 2>/dev/null; then pass "$2"; else fail "$2"; fi; }
# Disk I/O lands on its own schedule (page cache, fsync, a busy host), so a
# single look right after the burst is flaky — wait for the line instead.
wait_for() {
    _n=0
    while [ "$_n" -lt 20 ]; do
        grep -qE -- "$1" "$T/calls.log" 2>/dev/null && { pass "$2"; return; }
        sleep 0.5
        _n=$((_n + 1))
    done
    fail "$2"
}
check_not() { if grep -qE -- "$1" "$T/calls.log" 2>/dev/null; then fail "$2"; else pass "$2"; fi; }

# same resolution the monitor does: partition -> parent, else the disk itself
dev_to_disk() {
    n=$(lsblk -no PKNAME "$1" 2>/dev/null | head -1)
    [ -n "$n" ] || n=$(lsblk -no KNAME "$1" 2>/dev/null | head -1)
    echo "$n"
}
DISK=$(dev_to_disk "$(findmnt -no SOURCE -T /tmp | head -1)")
[ -n "$DISK" ] || DISK=$(ls /sys/block | grep '^sd' | sort | head -1)
[ -n "$DISK" ] || { echo "no sd* disk in /sys/block"; exit 1; }
NIC=$(ls /sys/class/net | grep -v lo | head -1)
echo "disk backing /tmp: $DISK   nic: $NIC"

start_mon() {                       # $1 = extra config lines
    rm -rf "$T"; mkdir -p "$T"
    cat > "$T/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "$@" >> /tmp/ledmon-test/calls.log
EOF
    chmod 755 "$T/ugreen_leds_cli"
    cp "$SRC" "$T/"
    { echo "INTERVAL=1"; echo "DISKS=\"$DISK\""; echo "NICS=\"$NIC\""; echo "$1"; } \
        > "$T/ugreen-leds-mon.conf"
    UGREEN_LEDS_CLI="$T/ugreen_leds_cli" UGREEN_LEDS_PIDFILE="$T/pid" \
        dash "$T/ugreen-leds-mon.sh" > "$T/mon.log" 2>&1 &
    MON=$!
    sleep 2
}
stop_mon() { kill "$MON" 2>/dev/null; wait "$MON" 2>/dev/null; }
burn()     { dd if=/dev/zero of=/tmp/ledmon-burn bs=1M count=800 conv=fsync 2>/dev/null; }
unburn()   { rm -f /tmp/ledmon-burn; sync; sleep 4; }

echo
echo "== case 1: defaults — idle stays solid, a burst blinks, then solid again"
start_mon ""
check '^disk1 -on -color 255 255 255 -brightness 64$' "initial solid write for disk1"
check '^netdev -on -color 255 255 255 -brightness 96$' "initial solid write for netdev"
check_not '-blink' "no blink while idle (below threshold)"
: > "$T/calls.log"
burn
wait_for '^disk1 -color 255 255 255 -blink 300 200$' "disk1 blinks on a real burst"
: > "$T/calls.log"
unburn
wait_for '^disk1 -on -color 255 255 255 -brightness 64$' "disk1 back to solid when idle"
stop_mon

echo
echo "== case 2: high threshold — the same burst must not blink"
start_mon "DISK_THRESHOLD_KB=100000000"
: > "$T/calls.log"
burn
sleep 4
check_not '^disk1 .*-blink' "burst below a huge threshold does not blink"
stop_mon
unburn

echo
echo "== case 3: custom colors reach the CLI"
start_mon 'COLOR_DISK_HEALTH="0 0 255"
COLOR_NETDEV_NORMAL="255 255 0"
COLOR_DISK_ACTIVE="255 0 0"
BRIGHTNESS_DISK_LEDS=40'
check '^disk1 -on -color 0 0 255 -brightness 40$' "disk idle color is blue at brightness 40"
check '^netdev -on -color 255 255 0 -brightness 96$' "netdev idle color is yellow"
: > "$T/calls.log"
burn
wait_for '^disk1 -color 255 0 0 -blink 300 200$' "disk blink uses the separate active color"
stop_mon
unburn

echo
echo "== case 4: DISK_ACTIVITY=0 keeps the LED static"
start_mon "DISK_ACTIVITY=0"
: > "$T/calls.log"
burn
sleep 4
check_not '^disk1 .*-blink' "no blink with disk activity disabled"
stop_mon
unburn

echo
echo "== case 5: auto-detection leaves the boot disk out"
rm -rf "$T"; mkdir -p "$T"
cat > "$T/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "$@" >> /tmp/ledmon-test/calls.log
EOF
chmod 755 "$T/ugreen_leds_cli"
cp "$SRC" "$T/"
echo "INTERVAL=1" > "$T/ugreen-leds-mon.conf"
UGREEN_LEDS_CLI="$T/ugreen_leds_cli" UGREEN_LEDS_PIDFILE="$T/pid" \
    dash "$T/ugreen-leds-mon.sh" > "$T/mon.log" 2>&1 &
MON=$!
sleep 2
ROOTDISK=$(dev_to_disk "$(findmnt -no SOURCE -T / | head -1)")
DETECTED=$(sed -n 's/^ugreen-leds-mon: disks: \(.*\)  nics.*/\1/p' "$T/mon.log")
echo "  detected: '$DETECTED'   root disk: '$ROOTDISK'"
if [ -z "$ROOTDISK" ]; then
    echo "  SKIP  no root disk resolved on this host"
elif echo " $DETECTED " | grep -q " $ROOTDISK "; then
    fail "boot disk $ROOTDISK excluded from auto-detected bays"
else
    pass "boot disk $ROOTDISK excluded from auto-detected bays"
fi
stop_mon

echo
echo "== case 6: start.sh takes its base colors from the same config"
rm -rf "$T"; mkdir -p "$T"
cat > "$T/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "$@" >> /tmp/ledmon-test/calls.log
EOF
chmod 755 "$T/ugreen_leds_cli"
cp "$(dirname "$SRC")/truenas/start.sh" "$T/"
cat > "$T/ugreen-leds-mon.conf" <<'EOF'
COLOR_POWER="0 255 0"
BRIGHTNESS_POWER=200
COLOR_DISK_HEALTH="0 0 255"
COLOR_NETDEV_NORMAL="255 255 0"
EOF
dash "$T/start.sh" >> "$T/mon.log" 2>&1        # no mon.sh here: base state only
check '^power -on -color 0 255 0 -brightness 200$' "start.sh power color and brightness"
check '^netdev netdev2 -on -color 255 255 0 -brightness 96$' "start.sh LAN color, default brightness"
check '^disk1 disk2 disk3 disk4 disk5 disk6 -on -color 0 0 255 -brightness 64$' "start.sh disk color"

echo
echo "== case 7: multiple bays keep their index (busy disk must not move the wrong LED)"
OTHERS=$(ls /sys/block | grep '^sd' | grep -v "^$DISK$" | sort | head -2 | tr '\n' ' ')
if [ "$(echo "$OTHERS" | wc -w)" -lt 2 ]; then
    echo "  SKIP  need at least three sd* disks on this host"
else
    set -- $OTHERS
    start_mon "DISKS=\"$1 $2 $DISK\""     # the busy one is bay 3
    check '^disk1 -on -color 255 255 255 -brightness 64$' "bay 1 gets its initial solid write"
    check '^disk3 -on -color 255 255 255 -brightness 64$' "bay 3 gets its initial solid write"
    : > "$T/calls.log"
    burn
    wait_for '^disk3 -color 255 255 255 -blink 300 200$' "the busy disk blinks on bay 3"
    check_not '^disk[12] .*-blink' "the idle bays stay solid"
    stop_mon
    unburn
fi

echo
echo "== case 8: bays with no disk behind them"
start_mon ""
check '^disk2 -off$' "empty bay 2 is switched off by default"
check '^disk6 -off$' "empty bay 6 is switched off by default"
check_not '^disk1 -off$' "the populated bay is left alone"
stop_mon

start_mon 'COLOR_DISK_EMPTY="255 0 0"'
check '^disk2 -on -color 255 0 0 -brightness 64$' "COLOR_DISK_EMPTY lights empty bays instead"
check_not '^disk2 -off$' "empty bays are not switched off when a color is set"
stop_mon

echo
echo "== case 9: excluding every disk falls back instead of going all dark"
rm -rf "$T"; mkdir -p "$T"
cat > "$T/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "$@" >> /tmp/ledmon-test/calls.log
EOF
chmod 755 "$T/ugreen_leds_cli"
cp "$SRC" "$T/"
ALLSD=$(ls /sys/block | grep '^sd' | sort | tr '\n' ' ')
{ echo "INTERVAL=1"; echo "EXCLUDE_DISKS=\"$ALLSD\""; } > "$T/ugreen-leds-mon.conf"
UGREEN_LEDS_CLI="$T/ugreen_leds_cli" UGREEN_LEDS_PIDFILE="$T/pid"     dash "$T/ugreen-leds-mon.sh" > "$T/mon.log" 2>&1 &
MON=$!
sleep 2
grep -q "falling back" "$T/mon.log" && pass "the fallback is reported in the log"                                     || fail "the fallback is reported in the log"
check '^disk1 -on ' "bay 1 is still driven rather than dark"
stop_mon

echo
if [ "$FAILED" = 0 ]; then echo "all checks passed"; else echo "$FAILED check(s) FAILED"; fi
rm -f /tmp/ledmon-burn
exit "$FAILED"
