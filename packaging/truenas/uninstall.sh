#!/bin/sh
# ug-paneld uninstaller for TrueNAS SCALE (run as root).
COMMENT="ug-paneld front panel display"

[ "$(id -u)" = "0" ] || { echo "Run as root (sudo)." >&2; exit 1; }

pkill -x ug-paneld 2>/dev/null

if command -v midclt >/dev/null 2>&1; then
    ID=$(midclt call initshutdownscript.query "[[\"comment\",\"=\",\"$COMMENT\"]]" 2>/dev/null \
         | sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p' | head -1)
    if [ -n "$ID" ]; then
        midclt call initshutdownscript.delete "$ID" >/dev/null 2>&1 \
            && echo "Removed Post-Init script (id $ID)." \
            || echo "Could not remove Post-Init script — delete it in System Settings > Advanced."
    fi
fi

rm -rf /etc/ug-paneld
echo "ug-paneld stopped. You can now delete the install directory on your pool."
