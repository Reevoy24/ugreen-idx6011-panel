#!/bin/sh
# ug-fand installer (fan control only — no display). Auto-detects the platform:
#   Unraid:            sudo sh install.sh                 (persists on /boot/config)
#   TrueNAS SCALE:     sudo sh install.sh /mnt/<pool>/ug-fand
#   Proxmox / Debian:  sudo sh install.sh                 (systemd)
#
# The binary lives next to this script (from the release tarball) or at the repo
# root after `make fand`.
set -e

[ "$(id -u)" = "0" ] || { echo "Run as root (sudo)." >&2; exit 1; }

HERE="$(cd "$(dirname "$0")" && pwd)"
find_file() {
    for c in "./$1" "$HERE/$1" "$HERE/../../$1"; do
        [ -f "$c" ] && { echo "$c"; return 0; }
    done
    return 1
}

BIN="$(find_file ug-fand)"         || { echo "ug-fand binary not found — run 'make fand' first." >&2; exit 1; }
CFG="$(find_file config.example)"  || CFG="$(find_file config)" || CFG="$HERE/config.example"
SVC="$(find_file ug-fand.service)" || SVC="$HERE/ug-fand.service"
START="$(find_file start.sh)"      || START="$HERE/start.sh"

# ---------- Unraid (flash boot; /boot/config persists, rootfs is rebuilt) ----------
if [ -d /boot/config ]; then
    PERSIST=/boot/config/ug-fand
    GO=/boot/config/go
    MARK_BEGIN="# >>> ug-fand >>>"
    MARK_END="# <<< ug-fand <<<"
    mkdir -p "$PERSIST"
    cp -f "$BIN"   "$PERSIST/ug-fand"
    cp -f "$START" "$PERSIST/start.sh"
    [ -f "$PERSIST/config" ] || cp -f "$CFG" "$PERSIST/config"
    if ! grep -q "$MARK_BEGIN" "$GO" 2>/dev/null; then
        { echo ""; echo "$MARK_BEGIN"; echo "sh $PERSIST/start.sh"; echo "$MARK_END"; } >> "$GO"
        echo "Added start hook to $GO"
    fi
    sh "$PERSIST/start.sh"
    echo "Installed (Unraid). Config: $PERSIST/config — edit it, then: sh $PERSIST/start.sh"
    exit 0
fi

# ---------- TrueNAS SCALE (pool + Post-Init; rootfs is read-only/ephemeral) ----------
DEST="$1"
if [ -n "$DEST" ] || command -v midclt >/dev/null 2>&1; then
    [ -n "$DEST" ] || { echo "TrueNAS: give a pool path, e.g.  sudo sh install.sh /mnt/tank/ug-fand" >&2; exit 1; }
    case "$DEST" in /mnt/*) ;; *) echo "Pool target must be under /mnt (TrueNAS rootfs is read-only)." >&2; exit 1 ;; esac
    mkdir -p "$DEST"
    cp -f "$BIN" "$DEST/ug-fand";          chmod 755 "$DEST/ug-fand"
    cp -f "$START" "$DEST/start.sh";       chmod 755 "$DEST/start.sh"
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
    echo "Installed (TrueNAS) to $DEST. Edit $DEST/config (mode=silent|default|turbo), then: sh $DEST/start.sh"
    exit 0
fi

# ---------- systemd (Proxmox / Debian) ----------
command -v systemctl >/dev/null 2>&1 || { echo "No systemd. For TrueNAS run: sudo sh install.sh /mnt/<pool>/ug-fand" >&2; exit 1; }
install -m 755 "$BIN" /usr/bin/ug-fand
mkdir -p /etc/ug-fand
[ -f /etc/ug-fand/config ] || install -m 644 "$CFG" /etc/ug-fand/config
install -m 644 "$SVC" /lib/systemd/system/ug-fand.service
systemctl daemon-reload
systemctl enable ug-fand
systemctl restart ug-fand   # restart so a reinstall always loads the new binary
echo "Installed (systemd). Status: systemctl status ug-fand   |   config: /etc/ug-fand/config"
