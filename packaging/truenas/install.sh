#!/bin/sh
# ug-paneld installer for TrueNAS SCALE (run as root from the extracted
# tarball dir). TrueNAS has a read-only root filesystem and apt is
# unsupported there, so the binary lives on one of your pools and is started
# by a middleware-registered Post-Init script.
set -e

DEST="$1"
COMMENT="ug-paneld front panel display"

[ "$(id -u)" = "0" ] || { echo "Run as root (sudo)." >&2; exit 1; }
[ -f ug-paneld ] || { echo "ug-paneld binary not found — run from the extracted tarball directory." >&2; exit 1; }
if [ -z "$DEST" ]; then
    echo "Usage: sh install.sh /mnt/<pool>/<dir>     e.g. sh install.sh /mnt/tank/ug-paneld" >&2
    exit 1
fi
case "$DEST" in
    /mnt/*) ;;
    *) echo "Install target must be on a pool under /mnt (TrueNAS rootfs is read-only)." >&2; exit 1 ;;
esac

mkdir -p "$DEST/wallpapers"
cp -f ug-paneld "$DEST/ug-paneld"
cp -f start.sh "$DEST/start.sh"
cp -f wallpapers/*.png "$DEST/wallpapers/" 2>/dev/null || true
# web dashboard assets (start.sh serves them from $DEST/web via UG_PANELD_WEB_DIR)
[ -d web ] && { mkdir -p "$DEST/web"; cp -f web/* "$DEST/web/" 2>/dev/null || true; }
chmod 755 "$DEST/ug-paneld" "$DEST/start.sh"
[ -f "$DEST/config.json" ] || cp -f config.json.example "$DEST/config.json"

# bundled fan control daemon (start.sh launches it if present)
[ -f ug-fand ]     && { cp -f ug-fand "$DEST/ug-fand"; chmod 755 "$DEST/ug-fand"; }
[ -f fand-config ] && [ ! -f "$DEST/fand-config" ] && cp -f fand-config "$DEST/fand-config"

# Register a Post-Init script so it starts on every boot (survives updates).
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
echo "Installed to $DEST. Config: $DEST/config.json (synced to /etc/ug-paneld/ at start)."
echo "After editing the config, run: sh $DEST/start.sh"
