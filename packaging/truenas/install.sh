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

# Register a Post-Init script so it runs on every boot.
# Which Post-Init entries are ours? Ours = our comment, or a command that runs
# start.sh from a directory holding this package (MARKER) — so an entry whose
# comment was edited, or an install that moved, is recognised instead of
# duplicated. Prints "MATCH <id> same|update" (or NONE), then "OTHER <id> <cmd>"
# for foreign entries that mention the install directory.
PY_FIND='
import json, os, re, sys
comment, dest, marker = sys.argv[1:4]
want = "sh %s/start.sh" % dest
try:
    rows = json.load(sys.stdin)
except Exception:
    rows = []
match, others = None, []
for r in rows if isinstance(rows, list) else []:
    cmd = (r.get("command") or r.get("script") or "").strip()
    m = re.fullmatch(r"(?:sh\s+)?(\S+)/start\.sh", cmd)
    ours = r.get("comment") == comment or cmd == want or bool(
        m and os.path.exists(os.path.join(m.group(1), marker)))
    if ours and match is None:
        match = r
    elif dest in cmd and not ours:
        others.append((r.get("id"), cmd))
if match is None:
    print("NONE")
else:
    same = ((match.get("command") or "").strip() == want and match.get("comment") == comment
            and match.get("when") == "POSTINIT" and match.get("enabled"))
    print("MATCH %s %s" % (match.get("id"), "same" if same else "update"))
for i, c in others:
    print("OTHER %s %s" % (i, c))
'

ENTRY="{\"type\":\"COMMAND\",\"command\":\"sh $DEST/start.sh\",\"when\":\"POSTINIT\",\"enabled\":true,\"comment\":\"$COMMENT\"}"
if command -v midclt >/dev/null 2>&1; then
    if command -v python3 >/dev/null 2>&1; then
        FOUND=$(midclt call initshutdownscript.query 2>/dev/null | python3 -c "$PY_FIND" "$COMMENT" "$DEST" ug-paneld)
    else   # no python3: fall back to matching the comment only
        if midclt call initshutdownscript.query "[[\"comment\",\"=\",\"$COMMENT\"]]" 2>/dev/null | grep -q "$COMMENT"; then
            FOUND="MATCH ? same"
        else
            FOUND="NONE"
        fi
    fi
    set -- $(echo "$FOUND" | head -1)
    if [ "$1" = MATCH ] && [ "$3" = same ]; then
        echo "Post-Init script already registered."
    elif [ "$1" = MATCH ] && midclt call initshutdownscript.update "$2" "$ENTRY" >/dev/null 2>&1; then
        echo "Updated the existing Post-Init script (id $2) to: sh $DEST/start.sh"
    elif [ "$1" = NONE ] && midclt call initshutdownscript.create "$ENTRY" >/dev/null 2>&1; then
        echo "Registered Post-Init script via TrueNAS middleware."
    else
        echo "WARNING: could not register automatically. Add this command manually under"
        echo "  System Settings > Advanced > Init/Shutdown Scripts (Type: Command, When: Post Init):"
        echo "  sh $DEST/start.sh"
    fi
    echo "$FOUND" | sed -n 's/^OTHER \([0-9]*\) \(.*\)/NOTE: Init\/Shutdown entry \1 also points into this install: \2\
      If that was a workaround, remove it in System Settings > Advanced./p'
else
    echo "midclt not found. Add this command under System Settings > Advanced >"
    echo "Init/Shutdown Scripts (Type: Command, When: Post Init):  sh $DEST/start.sh"
fi

sh "$DEST/start.sh"
echo
echo "Installed to $DEST. Config: $DEST/config.json (synced to /etc/ug-paneld/ at start)."
echo "After editing the config, run: sh $DEST/start.sh"
