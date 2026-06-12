#!/bin/sh
# Front-LED installer for Unraid (run as root from the extracted tarball
# dir). Persists everything under /boot/config/ugreen-leds and hooks into
# /boot/config/go: at every boot the rolling LED animation is stopped and a
# static LED state is set.
set -e

PERSIST=/boot/config/ugreen-leds
GO=/boot/config/go
MARK_BEGIN="# >>> ugreen-leds >>>"
MARK_END="# <<< ugreen-leds <<<"

[ "$(id -u)" = "0" ] || { echo "Run as root." >&2; exit 1; }
[ -d /boot/config ] || { echo "/boot/config not found — is this Unraid?" >&2; exit 1; }
[ -f ugreen_leds_cli ] || { echo "ugreen_leds_cli not found — run from the extracted tarball directory." >&2; exit 1; }

mkdir -p "$PERSIST"
cp -f ugreen_leds_cli "$PERSIST/ugreen_leds_cli"
cp -f start.sh "$PERSIST/start.sh"
cp -f README.txt "$PERSIST/README.txt" 2>/dev/null || true

if ! grep -q "$MARK_BEGIN" "$GO" 2>/dev/null; then
    {
        echo ""
        echo "$MARK_BEGIN"
        echo "sh /boot/config/ugreen-leds/start.sh"
        echo "$MARK_END"
    } >> "$GO"
    echo "Added start hook to $GO"
fi

sh "$PERSIST/start.sh"
echo
echo "Installed. The rolling animation stops at every boot now."
echo "Colors/brightness: edit $PERSIST/start.sh and re-run it."
