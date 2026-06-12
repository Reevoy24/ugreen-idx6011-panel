#!/bin/bash
# Build the statically-linked ugreen_leds_cli from klein0r's fork for the
# TrueNAS/Unraid LED tarballs. Pinned to the commit reviewed in this repo's
# LED work (the same code the Proxmox setup script builds on the NAS).
# Build only — nothing is executed.
set -e

PIN="4df6616a"
rm -rf /tmp/ulc
git clone --quiet https://github.com/klein0r/ugreen_leds_controller /tmp/ulc
cd /tmp/ulc
git checkout --quiet "$PIN"
echo "pinned at: $(git rev-parse HEAD)"

make -C cli >/dev/null
file cli/ugreen_leds_cli
sha256sum cli/ugreen_leds_cli

DEST="/mnt/c/Users/Basti/Desktop/ugreen-idx6011-pro-nas-display/packaging/leds"
mkdir -p "$DEST"
cp cli/ugreen_leds_cli "$DEST/ugreen_leds_cli"
echo "copied to packaging/leds/ugreen_leds_cli"
