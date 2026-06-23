#!/bin/sh
# ug-fand installer.
#   Proxmox / Debian (systemd):   sudo sh install.sh
#   TrueNAS SCALE (pool + Post-Init): sudo sh install.sh /mnt/<pool>/ug-fand
#
# Build the binary first with `make fand` (it lives at the repo root) — this
# script finds it next to itself, in the current dir, or at the repo root.
set -e

[ "$(id -u)" = "0" ] || { echo "Run as root (sudo)." >&2; exit 1; }

HERE="$(cd "$(dirname "$0")" && pwd)"
find_file() {
    for c in "./$1" "$HERE/$1" "$HERE/../../$1"; do
        [ -f "$c" ] && { echo "$c"; return 0; }
    done
    return 1
}

BIN="$(find_file ug-fand)"        || { echo "ug-fand binary not found — run 'make fand' first." >&2; exit 1; }
CFG="$(find_file config.example)" || CFG="$HERE/config.example"
SVC="$(find_file ug-fand.service)" || SVC="$HERE/ug-fand.service"

DEST="$1"
if [ -n "$DEST" ]; then
    # ---------- TrueNAS / pool install ----------
    case "$DEST" in /mnt/*) ;; *) echo "Pool target must be under /mnt (TrueNAS rootfs is read-only)." >&2; exit 1 ;; esac
    mkdir -p "$DEST"
    cp -f "$BIN" "$DEST/ug-fand";        chmod 755 "$DEST/ug-fand"
    cp -f "$HERE/start.sh" "$DEST/start.sh"; chmod 755 "$DEST/start.sh"
    [ -f "$DEST/config" ] || cp -f "$CFG" "$DEST/config"

    COMMENT="ug-fand fan control"
    if command -v midclt >/dev/null 2>&1; then
        if midclt call initshutdownscript.query "[[\"comment\",\"=\",\"$COMMENT\"]]" 2>/dev/null | grep -q "$COMMENT"; then
            echo "Post-Init script already registered."
        elif midclt call initshutdownscript.create "{\"type\":\"COMMAND\",\"command\":\"sh $DEST/start.sh\",\"when\":\"POSTINIT\",\"enabled\":true,\"comment\":\"$COMMENT\"}" >/dev/null 2>&1; then
            echo "Registered Post-Init script via TrueNAS middleware."
        else
            echo "WARNING: register manually under System Settings > Advanced > Init/Shutdown Scripts"
            echo "  (Type: Command, When: Post Init):  sh $DEST/start.sh"
        fi
    else
        echo "midclt not found. Add under System Settings > Advanced > Init/Shutdown Scripts:  sh $DEST/start.sh"
    fi
    sh "$DEST/start.sh"
    echo "Installed to $DEST. Edit $DEST/config (mode=silent|default|turbo), then: sh $DEST/start.sh"
else
    # ---------- systemd install (Proxmox / Debian) ----------
    command -v systemctl >/dev/null 2>&1 || { echo "No systemd. For TrueNAS run: sh install.sh /mnt/<pool>/ug-fand" >&2; exit 1; }
    install -m 755 "$BIN" /usr/bin/ug-fand
    mkdir -p /etc/ug-fand
    [ -f /etc/ug-fand/config ] || install -m 644 "$CFG" /etc/ug-fand/config
    install -m 644 "$SVC" /lib/systemd/system/ug-fand.service
    systemctl daemon-reload
    systemctl enable ug-fand
    systemctl restart ug-fand   # restart so a reinstall always loads the new binary
    echo "Installed. Status: systemctl status ug-fand   |   config: /etc/ug-fand/config"
fi
