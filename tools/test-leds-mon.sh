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
# kill via the pid file too: a monitor started from a function runs as a
# child of the backgrounded subshell, so killing $MON alone orphans it
stop_mon() {
    [ -f "$T/pid" ] && kill "$(cat "$T/pid")" 2>/dev/null
    pkill -P "$MON" 2>/dev/null
    kill "$MON" 2>/dev/null; wait "$MON" 2>/dev/null
}
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
echo "== case 10: non-Pro layout — bay 1 is netdev2, one LAN LED, disk6 unwired"
start_mon "LED_LAYOUT=nonpro"
check '^netdev2 -on -color 255 255 255 -brightness 64$' "bay 1 is driven through netdev2"
check '^netdev -on -color 255 255 255 -brightness 96$' "the single LAN LED is netdev"
check '^disk1 -off$' "empty bay 2 (disk1) is switched off"
check '^disk5 -off$' "empty bay 6 (disk5) is switched off"
check '^disk6 -off$' "the unwired disk6 register is switched off"
check_not '^disk1 -on' "disk1 is not treated as bay 1"
: > "$T/calls.log"
burn
wait_for '^netdev2 -color 255 255 255 -blink 300 200$' "a burst blinks bay 1 (netdev2)"
stop_mon
unburn

echo
echo "== case 11: non-Pro start.sh"
rm -rf "$T"; mkdir -p "$T"
cat > "$T/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "$@" >> /tmp/ledmon-test/calls.log
EOF
chmod 755 "$T/ugreen_leds_cli"
cp "$(dirname "$SRC")/truenas/start.sh" "$T/"
echo "LED_LAYOUT=nonpro" > "$T/ugreen-leds-mon.conf"
dash "$T/start.sh" >> "$T/mon.log" 2>&1
check '^netdev -on -color 255 255 255 -brightness 96$' "start.sh lights the single LAN LED"
check '^netdev2 disk1 disk2 disk3 disk4 disk5 -on ' "start.sh lights the six bays"
check '^disk6 -off$' "start.sh switches the unwired register off"
check_not '^netdev netdev2' "start.sh does not give bay 1 the LAN color"

# ---- bay wiring: a fake PCI tree with the two SATA controllers --------------
# $1 = Intel chipset ports, $2 = ASMedia ports, $3 = port the test disk is on.
# diskstats are still read for the real disk name.
fake_hw() {
    rm -rf "$T"; mkdir -p "$T/sysblock" "$T/pci/0000:00:17.0" "$T/pci/0000:5a:00.0" "$T/pci/0000:00:02.0"
    cat > "$T/ugreen_leds_cli" <<'EOS'
#!/bin/sh
echo "$@" >> /tmp/ledmon-test/calls.log
EOS
    chmod 755 "$T/ugreen_leds_cli"
    cp "$SRC" "$T/"
    echo 0x8086 > "$T/pci/0000:00:02.0/vendor"            # Intel GPU: no SATA ports, ignored
    echo 0x8086 > "$T/pci/0000:00:17.0/vendor"
    echo 0x1b21 > "$T/pci/0000:5a:00.0/vendor"
    for p in $1; do mkdir "$T/pci/0000:00:17.0/$p"; done
    for p in $2; do mkdir "$T/pci/0000:5a:00.0/$p"; done
    case " $1 " in *" $3 "*) c=0000:00:17.0 ;; *) c=0000:00:1c.0/0000:5a:00.0 ;; esac
    ln -s "../devices/pci0000:00/$c/$3/host9/target9:0:0/9:0:0:0/block/$DISK" "$T/sysblock/$DISK"
}
run_hw() {                          # $1 = layout, $2 = extra args for the script
    { echo "INTERVAL=1"; echo "LED_LAYOUT=$1"; echo "SKIP_BOOT_DISK=0"; echo "NICS=\"$NIC\""; } > "$T/ugreen-leds-mon.conf"
    UGREEN_LEDS_CLI="$T/ugreen_leds_cli" UGREEN_LEDS_PIDFILE="$T/pid" \
    UGREEN_SYS_BLOCK="$T/sysblock" UGREEN_SYS_PCI="$T/pci" UGREEN_IDENTIFY_SECS=1 \
        dash "$T/ugreen-leds-mon.sh" ${2:-} > "$T/mon.log" 2>&1
}

echo
echo "== case 12: non-Pro — ASMedia ports are bays 1-4, so ata5 is bay 3 (LED disk2)"
fake_hw "ata1 ata2" "ata3 ata4 ata5 ata6" ata5
run_hw nonpro &
MON=$!
sleep 2
grep -q "disks: - - $DISK - - - " "$T/mon.log" && pass "the disk is listed as bay 3" || fail "the disk is listed as bay 3"
check '^disk2 -on -color 255 255 255 -brightness 64$' "bay 3 (disk2) gets the disk"
check '^netdev2 -off$' "empty bay 1 is switched off"
check '^disk1 -off$' "empty bay 2 is switched off"
: > "$T/calls.log"
burn
wait_for '^disk2 -color 255 255 255 -blink 300 200$' "a burst blinks bay 3"
check_not '^netdev2 .*-blink' "the empty bay 1 does not blink"
stop_mon
unburn

echo
echo "== case 13: Pro — ASMedia ports are bays 1 2 5 6, Intel ports bays 3 4"
fake_hw "ata1 ata2" "ata3 ata4 ata5 ata6" ata5
run_hw pro &
MON=$!; sleep 2
grep -q "disks: - - - - $DISK - " "$T/mon.log" && pass "ASMedia port 3 is bay 5" || fail "ASMedia port 3 is bay 5"
check '^disk5 -on -color 255 255 255 -brightness 64$' "bay 5 is driven through disk5"
stop_mon
fake_hw "ata1 ata2" "ata3 ata4 ata5 ata6" ata2
run_hw pro &
MON=$!; sleep 2
grep -q "disks: - - - $DISK - - " "$T/mon.log" && pass "Intel port 2 is bay 4" || fail "Intel port 2 is bay 4"
stop_mon

echo
echo "== case 14: ataN numbering flips (ASMedia probed first) — bays follow the controller"
fake_hw "ata5 ata6" "ata1 ata2 ata3 ata4" ata5
run_hw pro &
MON=$!; sleep 2
grep -q "disks: - - $DISK - - - " "$T/mon.log" && pass "Intel's first port is still bay 3 on the Pro" || fail "Intel's first port is still bay 3 on the Pro"
stop_mon
fake_hw "ata5 ata6" "ata1 ata2 ata3 ata4" ata5
run_hw nonpro &
MON=$!; sleep 2
grep -q "disks: - - - - $DISK - " "$T/mon.log" && pass "Intel's first port is still bay 5 on the non-Pro" || fail "Intel's first port is still bay 5 on the non-Pro"
stop_mon
fake_hw "ata1 ata2" "ata10 ata9 ata11 ata12" ata10
run_hw pro &
MON=$!; sleep 2
grep -q "disks: - $DISK - - - - " "$T/mon.log" && pass "ports sort numerically (ata9 before ata10)" || fail "ports sort numerically (ata9 before ata10)"
stop_mon

echo
echo "== case 15: --show-map and --identify"
fake_hw "ata1 ata2" "ata3 ata4 ata5 ata6" ata5
run_hw nonpro --show-map
cat "$T/mon.log" | sed 's/^/      /'
grep -qE "^3 +disk2 +ata5 +$DISK" "$T/mon.log" && pass "show-map lists bay 3 with its LED, port and disk" || fail "show-map lists bay 3 with its LED, port and disk"
grep -qE "^1 +netdev2 +ata3 +- +\(empty\)" "$T/mon.log" && pass "show-map lists the empty bay 1" || fail "show-map lists the empty bay 1"
[ -f "$T/pid" ] && fail "show-map leaves no pid file" || pass "show-map leaves no pid file"
check_not '.' "show-map does not touch any LED"
[ -s "$T/calls.log" ] && sed 's/^/      wrote: /' "$T/calls.log"
: > "$T/calls.log"
run_hw nonpro "--identify 3"
check '^disk2 -color 0 0 255 -blink 150 150$' "identify blinks bay 3 blue"
check '^disk2 -on -color 255 255 255 -brightness 64$' "identify puts the bay back to its idle color"
: > "$T/calls.log"
run_hw nonpro "--identify 1"
check '^netdev2 -off$' "identify switches an empty bay back off"
run_hw nonpro "--identify 7"; [ $? = 2 ] && pass "identify rejects bay 7" || fail "identify rejects bay 7"

echo
if [ "$FAILED" = 0 ]; then echo "all checks passed"; else echo "$FAILED check(s) FAILED"; fi
rm -f /tmp/ledmon-burn
exit "$FAILED"
