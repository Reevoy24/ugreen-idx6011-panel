#!/bin/bash
# Sanity-check the staged ugreen_leds_cli before packaging (no execution).
set -e
cd /mnt/c/Users/Basti/Desktop/ugreen-idx6011-pro-nas-display

ls -la packaging/leds/ugreen_leds_cli
file packaging/leds/ugreen_leds_cli
sha256sum packaging/leds/ugreen_leds_cli
echo "--- expected markers:"
strings packaging/leds/ugreen_leds_cli | grep -m 5 -E 'UGREEN_MODEL|network_stat2|netdev2|SMBus I801'
