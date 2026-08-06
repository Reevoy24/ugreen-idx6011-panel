#!/bin/bash
# Build the TrueNAS SCALE and Unraid front-LED tarballs (static variant).
# Usage: ./build-leds-tarballs.sh [version]   (run on Linux/WSL)
#
# Needs packaging/leds/ugreen_leds_cli — the statically linked CLI built
# from klein0r/ugreen_leds_controller (commit 4df6616a, the same reviewed
# code tools/setup-ugreen-leds.sh installs on Proxmox/Debian). Easiest way
# to get it: copy it from a machine that ran the setup script, e.g.
#   scp root@<nas>:/usr/local/bin/ugreen_leds_cli packaging/leds/
set -e

VERSION="${1:-1.0.0}"
REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

CLI=packaging/leds/ugreen_leds_cli
[ -f "$CLI" ] || { echo "missing $CLI — see the header comment of this script" >&2; exit 1; }
file "$CLI" | grep -q "statically linked" || echo "WARNING: $CLI does not look statically linked"

build_tarball() {
    local platform="$1"
    local stage="/tmp/ugreen-leds-tarball-$platform/ugreen-leds"

    rm -rf "/tmp/ugreen-leds-tarball-$platform"
    mkdir -p "$stage"

    cp "$CLI" "$stage/ugreen_leds_cli"
    cp packaging/leds/ugreen-leds-mon.sh "$stage/"
    cp "packaging/leds/$platform/install.sh" "packaging/leds/$platform/uninstall.sh" \
       "packaging/leds/$platform/start.sh" "packaging/leds/$platform/README.txt" "$stage/"
    chmod 755 "$stage/ugreen_leds_cli" "$stage"/*.sh

    tar -C "/tmp/ugreen-leds-tarball-$platform" --owner=0 --group=0 \
        -czf "$REPO/ugreen-leds_${VERSION}_${platform}_amd64.tar.gz" ugreen-leds
    rm -rf "/tmp/ugreen-leds-tarball-$platform"
}

build_tarball truenas
build_tarball unraid

sha256sum "$REPO"/ugreen-leds_${VERSION}_{truenas,unraid}_amd64.tar.gz
