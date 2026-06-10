#!/bin/bash
# Dev helper: rebuild daemon + mock renderer, run the stats sanity check.
set -e
cd "$(dirname "$0")"

echo "=== daemon build ==="
make -j8 2>&1 | grep -E "error|warning|Built" | grep -v "lvgl/" || true

echo "=== stats check (host) ==="
gcc -Iinclude $(pkg-config --cflags libdrm) \
    test/stats_check.c src/net_stats.c src/disk_stats.c src/pve_stats.c src/gpu_stats.c \
    -o /tmp/stats-check
/tmp/stats-check

echo "=== mockups ==="
bash build-mockups.sh >/dev/null
ls mockups/*.png | head
