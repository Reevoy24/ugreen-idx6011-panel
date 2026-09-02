#!/bin/bash
# Unit test for the CLI backend in src/leds.c: finding the tools outside
# /usr/local/bin (TrueNAS/Unraid keep them on a pool or the flash drive,
# /usr is read-only there), re-applying the configured colors on "on", and
# stopping the activity monitor on "off" so it cannot re-light the LEDs.
# leds.c only needs libc, so it links against a small stub main.
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
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    if (!leds_init("21:00", "08:00")) { printf("BACKEND none\n"); return 1; }
    printf("BACKEND ok\n");
    if (argc > 1 && !strcmp(argv[1], "off")) { leds_startup(1, 0); leds_toggle(); }
    else leds_startup(1, 0);
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
echo "== case 3: \"off\" stops the activity monitor before killing the LEDs"
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
if [ "$FAILED" = 0 ]; then echo "all checks passed"; else echo "$FAILED check(s) FAILED"; fi
exit "$FAILED"
