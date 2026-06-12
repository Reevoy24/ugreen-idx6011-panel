#!/bin/sh
# Front-LED installer for TrueNAS SCALE (run as root from the extracted
# tarball dir). TrueNAS has a read-only root filesystem, so the CLI lives on
# one of your pools and runs at boot via a middleware-registered Post-Init
# script — that stops the rolling LED animation and sets a static LED state.
set -e

DEST="$1"
COMMENT="ugreen-leds front panel LEDs"

[ "$(id -u)" = "0" ] || { echo "Run as root (sudo)." >&2; exit 1; }
[ -f ugreen_leds_cli ] || { echo "ugreen_leds_cli not found — run from the extracted tarball directory." >&2; exit 1; }
if [ -z "$DEST" ]; then
    echo "Usage: sh install.sh /mnt/<pool>/<dir>     e.g. sh install.sh /mnt/tank/ugreen-leds" >&2
    exit 1
fi
case "$DEST" in
    /mnt/*) ;;
    *) echo "Install target must be on a pool under /mnt (TrueNAS rootfs is read-only)." >&2; exit 1 ;;
esac

mkdir -p "$DEST"
cp -f ugreen_leds_cli "$DEST/ugreen_leds_cli"
cp -f start.sh "$DEST/start.sh"
cp -f README.txt "$DEST/README.txt" 2>/dev/null || true
chmod 755 "$DEST/ugreen_leds_cli" "$DEST/start.sh"

# Register a Post-Init script so the LEDs are set on every boot.
if command -v midclt >/dev/null 2>&1; then
    if midclt call initshutdownscript.query "[[\"comment\",\"=\",\"$COMMENT\"]]" 2>/dev/null | grep -q "$COMMENT"; then
        echo "Post-Init script already registered."
    elif midclt call initshutdownscript.create "{\"type\":\"COMMAND\",\"command\":\"sh $DEST/start.sh\",\"when\":\"POSTINIT\",\"enabled\":true,\"comment\":\"$COMMENT\"}" >/dev/null 2>&1; then
        echo "Registered Post-Init script via TrueNAS middleware."
    else
        echo "WARNING: could not register automatically. Add this command manually under"
        echo "  System Settings > Advanced > Init/Shutdown Scripts (Type: Command, When: Post Init):"
        echo "  sh $DEST/start.sh"
    fi
else
    echo "midclt not found. Add this command under System Settings > Advanced >"
    echo "Init/Shutdown Scripts (Type: Command, When: Post Init):  sh $DEST/start.sh"
fi

sh "$DEST/start.sh"
echo
echo "Installed to $DEST. The rolling animation stops at every boot now."
echo "Colors/brightness: edit $DEST/start.sh and re-run it."
