#!/bin/bash
# Unit test for the CLI backend in src/leds.c: finding the tools outside
# /usr/local/bin (TrueNAS/Unraid keep them on a pool or the flash drive,
# /usr is read-only there), re-applying the configured colors on "on", and
# stopping the activity monitor on "off" so it cannot re-light the LEDs.
# Also covers reading and writing the three colors the settings UI
# exposes, locating a bay and the 24-hour night window. leds.c only needs
# libc and the bay layout, so it links against a stub main.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
T=/tmp/ledsbackend-test
rm -rf "$T"; mkdir -p "$T/install"

FAILED=0
pass() { echo "  PASS  $1"; }
fail() { echo "  FAIL  $1"; FAILED=$((FAILED + 1)); }
check()     { if grep -qE -- "$1" "$T/calls.log" 2>/dev/null; then pass "$2"; else fail "$2"; fi; }
check_not() { if grep -qE -- "$1" "$T/calls.log" 2>/dev/null; then fail "$2"; else pass "$2"; fi; }

cat > "$T/stub.c" <<'EOF'
#include "leds.h"
#include "disk_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int disk_stats_bay_layout(void) { return BAY_LAYOUT_PRO; }
int main(int argc, char **argv)
{
    if (!leds_init("21:00", "08:00")) { printf("BACKEND none\n"); return 1; }
    printf("BACKEND ok\n");
    const char *mode = argc > 1 ? argv[1] : "";
    if (!strcmp(mode, "off")) { leds_startup(1, 0); leds_toggle(); return 0; }
    if (!strcmp(mode, "identify") && argc > 2) {   /* identify <lit> [twice] */
        leds_set_identify_secs(1);
        leds_startup(atoi(argv[2]), 0);
        printf("IDENTIFY rc=%d\n", leds_identify(2));
        if (argc > 3) printf("IDENTIFY rc=%d\n", leds_identify(2));
        return 0;
    }
    if (!strcmp(mode, "window") && argc > 3) {     /* window <start> <end> */
        leds_startup(1, 1);
        leds_set_window(argv[2], argv[3]);
        printf("EFFECTIVE %d LABEL [%s]\n", leds_effective_on(), leds_night_label());
        leds_toggle();
        printf("OVERRIDDEN %d\n", leds_effective_on());
        leds_set_night(0);
        printf("NIGHT-OFF %d\n", leds_effective_on());
        return 0;
    }
    if (!strcmp(mode, "getcolors")) {
        leds_colors_t c;
        printf("SUPPORTED %d\n", leds_colors_supported());
        leds_get_colors(&c);
        printf("POWER [%s]\nDISK [%s]\nNETDEV [%s]\n", c.power, c.disk, c.netdev);
        return 0;
    }
    if (!strcmp(mode, "setcolors") && argc > 4) {
        leds_colors_t c;
        leds_startup(1, 0);
        snprintf(c.power, sizeof(c.power), "%s", argv[2]);
        snprintf(c.disk, sizeof(c.disk), "%s", argv[3]);
        snprintf(c.netdev, sizeof(c.netdev), "%s", argv[4]);
        printf("SET rc=%d\n", leds_set_colors(&c));
        return 0;
    }
    leds_startup(1, 0);
    return 0;
}
EOF
cc -I"$REPO/include" -o "$T/stub" "$T/stub.c" "$REPO/src/leds.c" || { echo "compile failed"; exit 1; }

# a fake install on a "pool": CLI plus the start script that applies colors
cat > "$T/install/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "cli $@" >> /tmp/ledsbackend-test/calls.log
EOF
cat > "$T/install/start.sh" <<'EOF'
#!/bin/sh
echo "start.sh ran" >> /tmp/ledsbackend-test/calls.log
EOF
chmod 755 "$T/install/ugreen_leds_cli" "$T/install/start.sh"

echo "== case 1: the install directory is found through UG_PANELD_LEDS_DIR"
: > "$T/calls.log"
OUT=$(UG_PANELD_LEDS_DIR="$T/install" "$T/stub" 2>&1)
echo "$OUT" | grep -q "BACKEND ok" && pass "backend detected outside /usr/local/bin" \
                                   || fail "backend detected outside /usr/local/bin"
echo "$OUT" | grep -q "start script $T/install/start.sh" \
    && pass "start.sh picked up as the apply script" \
    || fail "start.sh picked up as the apply script"
check 'start.sh ran' "\"on\" runs start.sh, so configured colors come back"
check_not 'cli all -on' "\"on\" does not fall back to the bare CLI"

echo
echo "== case 2: no install anywhere means no backend (nothing to drive)"
OUT=$(UG_PANELD_LEDS_DIR="$T/nonexistent" "$T/stub" 2>&1)
echo "$OUT" | grep -q "BACKEND none" && pass "reports no backend" || fail "reports no backend"

echo
echo "== case 3: an install without start.sh still drives the LEDs"
mkdir -p "$T/cli-only"
cp "$T/install/ugreen_leds_cli" "$T/cli-only/ugreen_leds_cli"
: > "$T/calls.log"
OUT=$(UG_PANELD_LEDS_DIR="$T/cli-only" "$T/stub" 2>&1)
echo "$OUT" | grep -q "BACKEND ok" && pass "backend detected without a start script"                                    || fail "backend detected without a start script"
echo "$OUT" | grep -q "start script (none)" && pass "reports that there is no start script"                                             || fail "reports that there is no start script"
check 'cli all -on' "falls back to the bare CLI for \"on\""

echo
echo "== case 4: \"off\" stops the activity monitor before killing the LEDs"
cp "$REPO/packaging/leds/ugreen-leds-mon.sh" "$T/ugreen-leds-mon.sh"
sleep 300 &                       # stand-in with the wrong name
STRANGER=$!
echo "$STRANGER" > "$T/pid"
: > "$T/calls.log"
UGREEN_LEDS_PIDFILE="$T/pid" UG_PANELD_LEDS_DIR="$T/install" "$T/stub" off >/dev/null 2>&1
if kill -0 "$STRANGER" 2>/dev/null; then
    pass "a pid that is not the monitor is left alone"
else
    fail "a pid that is not the monitor is left alone"
fi
kill "$STRANGER" 2>/dev/null

sh -c 'exec sh /tmp/ledsbackend-test/ugreen-leds-mon.sh' >/dev/null 2>&1 &
MON=$!
sleep 1
echo "$MON" > "$T/pid"
: > "$T/calls.log"
UGREEN_LEDS_PIDFILE="$T/pid" UG_PANELD_LEDS_DIR="$T/install" "$T/stub" off >/dev/null 2>&1
sleep 4          # sh handles the trap only once the current poll sleep returns
if kill -0 "$MON" 2>/dev/null; then
    fail "the running monitor is stopped"
    kill "$MON" 2>/dev/null
else
    pass "the running monitor is stopped"
fi
check 'cli all -off' "the LEDs are switched off through the CLI"

echo
echo "== case 5: colors are read from the install's config"
cat > "$T/install/ugreen-leds-mon.conf" <<'CEOF'
# a comment we must not lose
INTERVAL=2
COLOR_POWER="255 255 255"
COLOR_DISK_HEALTH="0 0 255"
COLOR_NETDEV_NORMAL="255 255 0"
COLOR_DISK_HEALTH_PER_DISK[disk1]="1 2 3"
DISK_THRESHOLD_KB=128
CEOF
OUT=$(UG_PANELD_LEDS_DIR="$T/install" "$T/stub" getcolors 2>/dev/null)
echo "$OUT" | grep -q "SUPPORTED 1" && pass "a config we can edit is reported" || fail "a config we can edit is reported"
echo "$OUT" | grep -q "DISK \[0 0 255\]" && pass "the disk color is read back" || fail "the disk color is read back"
echo "$OUT" | grep -q "NETDEV \[255 255 0\]" && pass "the LAN color is read back" || fail "the LAN color is read back"

echo
echo "== case 6: writing colors keeps every other setting intact"
: > "$T/calls.log"
UGREEN_LEDS_PIDFILE="$T/pid" UG_PANELD_LEDS_DIR="$T/install" \
    "$T/stub" setcolors "255 255 255" "0 255 0" "255 0 0" >/dev/null 2>&1
CONF="$T/install/ugreen-leds-mon.conf"
grep -q '^COLOR_DISK_HEALTH="0 255 0"$' "$CONF" && pass "the new disk color is written" || fail "the new disk color is written"
grep -q '^COLOR_NETDEV_NORMAL="255 0 0"$' "$CONF" && pass "the new LAN color is written" || fail "the new LAN color is written"
grep -q '^DISK_THRESHOLD_KB=128$' "$CONF" && pass "unrelated keys survive" || fail "unrelated keys survive"
grep -q '^# a comment we must not lose$' "$CONF" && pass "comments survive" || fail "comments survive"
grep -q '^COLOR_DISK_HEALTH_PER_DISK\[disk1\]="1 2 3"$' "$CONF" && pass "the per-disk key is not mistaken for COLOR_DISK_HEALTH" || fail "the per-disk key is not mistaken for COLOR_DISK_HEALTH"
[ "$(grep -c '^COLOR_DISK_HEALTH=' "$CONF")" = 1 ] && pass "the key is replaced, not duplicated" || fail "the key is replaced, not duplicated"
check 'start.sh ran' "the install's start.sh re-applies them"

echo
echo "== case 7: missing colors are appended rather than lost"
printf 'INTERVAL=2\n' > "$CONF"
UG_PANELD_LEDS_DIR="$T/install" "$T/stub" setcolors "1 2 3" "4 5 6" "7 8 9" >/dev/null 2>&1
grep -q '^COLOR_POWER="1 2 3"$' "$CONF" && pass "a missing key is appended" || fail "a missing key is appended"
grep -q '^INTERVAL=2$' "$CONF" && pass "the existing key is still there" || fail "the existing key is still there"

echo
echo "== case 8: identify on a bare CLI puts a lit bay back as it was"
cat > "$T/cli-only/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "cli $@" >> /tmp/ledsbackend-test/calls.log
case "$*" in *-status*) echo "$1: status = ${FAKE_STATUS:-on}, brightness = 64, color = RGB(255, 128, 0)" ;; esac
EOF
: > "$T/calls.log"
UG_PANELD_LEDS_DIR="$T/cli-only" "$T/stub" identify 1 >/dev/null 2>&1
sleep 2
check 'cli disk2 -status' "the LED's state is read first"
check 'cli disk2 -color 0 0 255 -brightness 255 -blink 150 150' "it blinks blue at full brightness"
check 'cli disk2 -color 255 128 0 -brightness 64 -on' "color, brightness and on come back"

echo
echo "== case 9: a bay that was off stays off, and so does one with the LEDs off"
: > "$T/calls.log"
FAKE_STATUS=off UG_PANELD_LEDS_DIR="$T/cli-only" "$T/stub" identify 1 >/dev/null 2>&1
sleep 2
check 'cli disk2 -color 255 128 0 -brightness 64 -off' "a dark bay goes dark again"
: > "$T/calls.log"
UG_PANELD_LEDS_DIR="$T/cli-only" "$T/stub" identify 0 >/dev/null 2>&1
sleep 2
check 'cli disk2 -color 255 128 0 -brightness 64 -off' "LEDs switched off win over the bay's last state"

echo
echo "== case 10: no readable state still ends the blinking"
cat > "$T/cli-only/ugreen_leds_cli" <<'EOF'
#!/bin/sh
echo "cli $@" >> /tmp/ledsbackend-test/calls.log
EOF
: > "$T/calls.log"
UG_PANELD_LEDS_DIR="$T/cli-only" "$T/stub" identify 1 >/dev/null 2>&1
sleep 2
check 'cli disk2 -off' "the bay is switched off"

echo
echo "== case 11: a second click on a blinking bay starts nothing"
: > "$T/calls.log"
UG_PANELD_LEDS_DIR="$T/cli-only" "$T/stub" identify 1 twice >/dev/null 2>&1
sleep 2
[ "$(grep -c -- '-blink' "$T/calls.log")" = 1 ] && pass "it blinks once" || fail "it blinks once"

echo
echo "== case 12: the monitor gets the duration and a bright LED"
cat > "$T/install/ugreen-leds-mon.sh" <<'EOF'
echo "mon $@ secs=$UGREEN_IDENTIFY_SECS" >> /tmp/ledsbackend-test/calls.log
EOF
: > "$T/calls.log"
UG_PANELD_LEDS_DIR="$T/install" "$T/stub" identify 1 >/dev/null 2>&1
sleep 1
check 'cli disk2 -brightness 255' "the LED goes to full brightness first"
check 'mon --identify 2 secs=1' "the monitor blinks it for the configured time"
check_not 'cli disk2 -status' "the monitor's restore needs no snapshot"
rm -f "$T/install/ugreen-leds-mon.sh"

echo
echo "== case 13: the same time twice is a 24-hour window"
OUT=$(UG_PANELD_LEDS_DIR="$T/install" "$T/stub" window 09:00 09:00 2>/dev/null)
echo "$OUT" | grep -q 'EFFECTIVE 0 LABEL \[24 h\]' && pass "the LEDs stay off and it reads 24 h" || fail "the LEDs stay off and it reads 24 h"
echo "$OUT" | grep -q 'OVERRIDDEN 1' && pass "turning them on still overrides it" || fail "turning them on still overrides it"
echo "$OUT" | grep -q 'NIGHT-OFF 1' && pass "the master toggle still disables night mode" || fail "the master toggle still disables night mode"
NOW=$(date +%H:%M)
OUT=$(UG_PANELD_LEDS_DIR="$T/install" "$T/stub" window "$(date -d '+2 hours' +%H:%M)" "$(date -d '+3 hours' +%H:%M)" 2>/dev/null)
echo "$OUT" | grep -q 'EFFECTIVE 1 LABEL \[[0-9]' && pass "an ordinary window outside now keeps them lit (at $NOW)" || fail "an ordinary window outside now keeps them lit (at $NOW)"

echo
if [ "$FAILED" = 0 ]; then echo "all checks passed"; else echo "$FAILED check(s) FAILED"; fi
exit "$FAILED"
