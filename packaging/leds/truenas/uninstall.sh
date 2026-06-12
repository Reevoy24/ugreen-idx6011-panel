#!/bin/sh
# Front-LED uninstaller for TrueNAS SCALE (run as root).
COMMENT="ugreen-leds front panel LEDs"

[ "$(id -u)" = "0" ] || { echo "Run as root (sudo)." >&2; exit 1; }

[ -f /var/run/ugreen-leds-mon.pid ] && kill "$(cat /var/run/ugreen-leds-mon.pid)" 2>/dev/null

if command -v midclt >/dev/null 2>&1; then
    ID=$(midclt call initshutdownscript.query "[[\"comment\",\"=\",\"$COMMENT\"]]" 2>/dev/null \
         | sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p' | head -1)
    if [ -n "$ID" ]; then
        midclt call initshutdownscript.delete "$ID" >/dev/null 2>&1 \
            && echo "Removed Post-Init script (id $ID)." \
            || echo "Could not remove Post-Init script — delete it in System Settings > Advanced."
    fi
fi

echo "Done. You can now delete the install directory on your pool."
echo "Note: after the next full power cycle the LEDs return to the stock rolling animation."
