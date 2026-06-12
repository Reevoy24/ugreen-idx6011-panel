#!/bin/bash
# Build the daemon + render mockups; print only the interesting lines.
cd /mnt/c/Users/Basti/Desktop/ugreen-idx6011-pro-nas-display || exit 1
make 2>&1 | grep -E 'error|warning|Built ug-paneld' | grep -v build_wp_options
./build-mockups.sh > /tmp/mock.log 2>&1 && echo "mockups OK" || tail -20 /tmp/mock.log
