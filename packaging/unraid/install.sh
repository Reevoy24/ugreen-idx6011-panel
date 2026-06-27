#!/bin/sh
# ug-paneld installer for Unraid (run as root from the extracted tarball dir).
# Persists everything under /boot/config/ug-paneld and hooks into
# /boot/config/go so the daemon starts on every boot.
set -e

PERSIST=/boot/config/ug-paneld
GO=/boot/config/go
MARK_BEGIN="# >>> ug-paneld >>>"
MARK_END="# <<< ug-paneld <<<"

[ "$(id -u)" = "0" ] || { echo "Run as root." >&2; exit 1; }
[ -d /boot/config ] || { echo "/boot/config not found — is this Unraid?" >&2; exit 1; }
[ -f ug-paneld ] || { echo "ug-paneld binary not found — run from the extracted tarball directory." >&2; exit 1; }

mkdir -p "$PERSIST/wallpapers"
cp -f ug-paneld "$PERSIST/ug-paneld"
cp -f start.sh "$PERSIST/start.sh"
cp -f wallpapers/*.png "$PERSIST/wallpapers/" 2>/dev/null || true
# web dashboard assets (start.sh serves them from $PERSIST/web via UG_PANELD_WEB_DIR)
[ -d web ] && { mkdir -p "$PERSIST/web"; cp -f web/* "$PERSIST/web/" 2>/dev/null || true; }
[ -f "$PERSIST/config.json" ] || cp -f config.json.example "$PERSIST/config.json"

# bundled fan control daemon (start.sh copies it off the flash drive and runs it)
[ -f ug-fand ]     && cp -f ug-fand "$PERSIST/ug-fand"
[ -f fand-config ] && [ ! -f "$PERSIST/fand-config" ] && cp -f fand-config "$PERSIST/fand-config"

if ! grep -q "$MARK_BEGIN" "$GO" 2>/dev/null; then
    {
        echo ""
        echo "$MARK_BEGIN"
        echo "sh /boot/config/ug-paneld/start.sh"
        echo "$MARK_END"
    } >> "$GO"
    echo "Added start hook to $GO"
fi

sh "$PERSIST/start.sh"
echo
echo "Installed. Config: $PERSIST/config.json (synced to /etc/ug-paneld/ at start)."
echo "After editing the config, run: sh $PERSIST/start.sh"
