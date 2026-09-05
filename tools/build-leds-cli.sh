#!/bin/bash
# Build the statically-linked ugreen_leds_cli from klein0r's fork for the
# TrueNAS/Unraid LED tarballs. Pinned to the commit reviewed in this repo's
# LED work (the same code the Proxmox setup script builds on the NAS).
# Build only — nothing is executed.
#
# Runs both locally (WSL) and in CI; the release workflow calls it before
# build-leds-tarballs.sh so the shipped binary is built from source by the
# pipeline rather than uploaded from a developer machine.
#
# Usage: tools/build-leds-cli.sh [dest-dir]     (default: packaging/leds)
set -e

PIN="480f114bae69ec2bb7003df5d9c13f788ca6ace6"
DEST="${1:-$(cd "$(dirname "$0")/.." && pwd)/packaging/leds}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git clone --quiet https://github.com/klein0r/ugreen_leds_controller "$WORK/ulc"
git -C "$WORK/ulc" checkout --quiet "$PIN"
echo "pinned at: $(git -C "$WORK/ulc" rev-parse HEAD)"

make -C "$WORK/ulc/cli" >/dev/null
file "$WORK/ulc/cli/ugreen_leds_cli"
sha256sum "$WORK/ulc/cli/ugreen_leds_cli"

# The tarballs go onto TrueNAS/Unraid, where we cannot rely on the host's
# libstdc++ — upstream's Makefile passes -static, so make sure it stuck.
file "$WORK/ulc/cli/ugreen_leds_cli" | grep -q "statically linked" || {
    echo "ERROR: ugreen_leds_cli is not statically linked" >&2
    exit 1
}

mkdir -p "$DEST"
cp "$WORK/ulc/cli/ugreen_leds_cli" "$DEST/ugreen_leds_cli"
chmod 755 "$DEST/ugreen_leds_cli"
echo "copied to $DEST/ugreen_leds_cli"
