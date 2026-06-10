#!/bin/sh
# ug-paneld uninstaller for Unraid (run as root).
PERSIST=/boot/config/ug-paneld
GO=/boot/config/go

[ "$(id -u)" = "0" ] || { echo "Run as root." >&2; exit 1; }

pkill -x ug-paneld 2>/dev/null
sed -i '/# >>> ug-paneld >>>/,/# <<< ug-paneld <<</d' "$GO" 2>/dev/null
rm -f /usr/local/bin/ug-paneld
rm -rf /etc/ug-paneld "$PERSIST"
echo "ug-paneld removed."
