#!/bin/sh
# ug-paneld uninstaller for TrueNAS SCALE (run as root).
COMMENT="ug-paneld front panel display"

[ "$(id -u)" = "0" ] || { echo "Run as root (sudo)." >&2; exit 1; }

pkill -x ug-paneld 2>/dev/null

if command -v midclt >/dev/null 2>&1; then
    # every entry of ours: by comment, or one that runs start.sh from a
    # directory holding this package (a renamed comment, or a duplicate)
    if command -v python3 >/dev/null 2>&1; then
        IDS=$(midclt call initshutdownscript.query 2>/dev/null | python3 -c '
import json, os, re, sys
try:
    rows = json.load(sys.stdin)
except Exception:
    rows = []
for r in rows if isinstance(rows, list) else []:
    cmd = (r.get("command") or r.get("script") or "").strip()
    m = re.fullmatch(r"(?:sh\s+)?(\S+)/start\.sh", cmd)
    if r.get("comment") == sys.argv[1] or (m and os.path.exists(os.path.join(m.group(1), sys.argv[2]))):
        print(r.get("id"))
' "$COMMENT" ug-paneld)
    else
        IDS=$(midclt call initshutdownscript.query "[[\"comment\",\"=\",\"$COMMENT\"]]" 2>/dev/null \
              | sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p')
    fi
    for ID in $IDS; do
        midclt call initshutdownscript.delete "$ID" >/dev/null 2>&1 \
            && echo "Removed Post-Init script (id $ID)." \
            || echo "Could not remove Post-Init script $ID — delete it in System Settings > Advanced."
    done
fi

rm -rf /etc/ug-paneld
echo "ug-paneld stopped. You can now delete the install directory on your pool."
