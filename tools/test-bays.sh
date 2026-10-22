#!/bin/bash
# The bay map exists twice — in C (disk_stats.c, for the web dashboard) and in
# sh (ugreen-leds-mon.sh, for the LEDs). This builds the C side against a fake
# sysfs tree and checks it gives the same answers the monitor's tests expect:
#   Pro      ASMedia ports -> bays 1 2 5 6, Intel ports -> bays 3 4
#   non-Pro  ASMedia ports -> bays 1 2 3 4, Intel ports -> bays 5 6
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cc -O2 -Wall -Wextra -Iinclude -o "$T/bays-probe" test/bays_probe.c src/disk_stats.c || exit 1

FAILED=0
pass() { echo "  PASS  $1"; }
fail() { echo "  FAIL  $1"; FAILED=$((FAILED + 1)); }

# $1 = Intel ports, $2 = ASMedia ports, then "disk:port" pairs
fake() {
    rm -rf "$T/root"; mkdir -p "$T/root/sys/block" "$T/root/sys/bus/pci/devices"
    local pci="$T/root/sys/bus/pci/devices"
    mkdir -p "$pci/0000:00:02.0" "$pci/0000:00:17.0" "$pci/0000:5a:00.0"
    echo 0x8086 > "$pci/0000:00:02.0/vendor"          # GPU: same vendor, no SATA ports
    echo 0x030000 > "$pci/0000:00:02.0/class"
    echo 0x8086 > "$pci/0000:00:17.0/vendor"
    echo 0x010601 > "$pci/0000:00:17.0/class"
    echo 0x1b21 > "$pci/0000:5a:00.0/vendor"
    echo 0x010601 > "$pci/0000:5a:00.0/class"
    for p in $1; do mkdir "$pci/0000:00:17.0/$p"; done
    for p in $2; do mkdir "$pci/0000:5a:00.0/$p"; done
    shift 2
    for dp in "$@"; do
        local d=${dp%%:*} p=${dp##*:} c
        case " $(ls "$pci/0000:00:17.0") " in *" $p "*) c=0000:00:17.0 ;; *) c=0000:00:1c.0/0000:5a:00.0 ;; esac
        ln -s "../devices/pci0000:00/$c/$p/host9/target9:0:0/9:0:0:0/block/$d" "$T/root/sys/block/$d"
    done
}
probe() { UG_SYSFS_ROOT="$T/root" UG_BAY_LAYOUT="$1" "$T/bays-probe" | sort; }
expect() {   # layout, expected "disk bay" lines (sorted), label
    local got; got=$(probe "$1" | grep -v '^layout')
    if [ "$got" = "$2" ]; then pass "$3"; else fail "$3"; echo "        got: $(echo $got)"; echo "   expected: $(echo $2)"; fi
}

echo "== Pro, the unit this was confirmed on (ata1/2 Intel, ata3-6 ASMedia)"
fake "ata1 ata2" "ata3 ata4 ata5 ata6" sdc:ata3 sdd:ata4 sda:ata1 sdb:ata2 sde:ata5
expect pro "$(printf 'sda 3\nsdb 4\nsdc 1\nsdd 2\nsde 5')" "WD/HPE/850/Exos/870 land in bays 1-5 as on the real NAS"

echo "== non-Pro (aoreskovic's unit)"
expect nonpro "$(printf 'sda 5\nsdb 6\nsdc 1\nsdd 2\nsde 3')" "ASMedia 1-4, Intel 5-6"

echo "== ataN numbering flipped (ASMedia probed first)"
fake "ata5 ata6" "ata1 ata2 ata3 ata4" sda:ata1 sdb:ata5 sdc:ata6
expect pro "$(printf 'sda 1\nsdb 3\nsdc 4')" "Pro follows the controller, not the number"
expect nonpro "$(printf 'sda 1\nsdb 5\nsdc 6')" "non-Pro follows the controller, not the number"

echo "== ports sort numerically"
fake "ata1 ata2" "ata9 ata10 ata11 ata12" sdx:ata10
expect pro "sdx 2" "ata10 comes after ata9"

echo "== not an iDX6011 / a disk outside the bays"
fake "ata1 ata2" "ata3 ata4 ata5 ata6" sda:ata1
got=$(UG_SYSFS_ROOT="$T/root" UG_BAY_LAYOUT=none "$T/bays-probe" sda 2>/dev/null | tail -1)
# UG_BAY_LAYOUT=none is not an override, so the real DMI decides; on a non-iDX
# host (CI, WSL) that is "no layout" and every disk reports bay 0
if [ "$(UG_BAY_LAYOUT=none "$T/bays-probe" | head -1)" = "layout none" ]; then
    [ "$got" = "sda 0" ] && pass "no bay map off an iDX6011" || fail "no bay map off an iDX6011 (got '$got')"
else
    echo "  SKIP  running on an iDX6011, DMI decides"
fi
expect pro "$(printf 'sda 3')" "baseline"
rm "$T/root/sys/block/sda"
ln -s "../devices/pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/host9/target9:0:0/9:0:0:0/block/sdu" "$T/root/sys/block/sdu"
expect pro "sdu 0" "a USB disk is in no bay"

echo "== a controller handed to a VM (vfio-pci): its bays are passed through, not empty"
pci="$T/root/sys/bus/pci/devices"
mkdir -p "$T/root/sys/bus/pci/drivers/vfio-pci" "$T/root/sys/bus/pci/drivers/ahci"
fake "ata1 ata2" "" sda:ata1
ln -s ../../../bus/pci/drivers/vfio-pci "$pci/0000:5a:00.0/driver"
ln -s ../../../bus/pci/drivers/ahci "$pci/0000:00:17.0/driver"
expect pro "sda 3" "the host-side disk keeps its bay"
got=$(UG_SYSFS_ROOT="$T/root" UG_BAY_LAYOUT=pro "$T/bays-probe" --collect | grep "^passthrough")
[ "$got" = "passthrough 1 2 5 6" ] && pass "Pro: ASMedia passed through -> bays 1 2 5 6" || fail "Pro: ASMedia passed through -> bays 1 2 5 6 (got '$got')"
got=$(UG_SYSFS_ROOT="$T/root" UG_BAY_LAYOUT=nonpro "$T/bays-probe" --collect | grep "^passthrough")
[ "$got" = "passthrough 1 2 3 4" ] && pass "non-Pro: ASMedia passed through -> bays 1 2 3 4" || fail "non-Pro: ASMedia passed through -> bays 1 2 3 4 (got '$got')"
fake "" "ata1 ata2 ata3 ata4" sdc:ata1
ln -s ../../../bus/pci/drivers/vfio-pci "$pci/0000:00:17.0/driver"
got=$(UG_SYSFS_ROOT="$T/root" UG_BAY_LAYOUT=pro "$T/bays-probe" --collect | grep "^passthrough")
[ "$got" = "passthrough 3 4" ] && pass "Pro: Intel passed through -> bays 3 4" || fail "Pro: Intel passed through -> bays 3 4 (got '$got')"
expect pro "sdc 1" "the ASMedia disk stays in bay 1"
fake "ata1 ata2" "ata3 ata4 ata5 ata6"
got=$(UG_SYSFS_ROOT="$T/root" UG_BAY_LAYOUT=pro "$T/bays-probe" --collect | grep "^passthrough")
[ "$got" = "passthrough" ] && pass "nothing passed through -> no bays flagged" || fail "nothing passed through -> no bays flagged (got '$got')"

echo
if [ "$FAILED" = 0 ]; then echo "all checks passed"; else echo "$FAILED check(s) FAILED"; fi
exit "$FAILED"
