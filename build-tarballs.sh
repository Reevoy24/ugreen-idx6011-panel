#!/bin/bash
# Build the TrueNAS SCALE and Unraid tarballs from the compiled binary.
# Usage: ./build-tarballs.sh [version]   (run on Linux/WSL after `make`)
set -e

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"
# Version: explicit arg wins; else derive from include/version.h (single source,
# shown in the web UI), turning "-betaN" into "~betaN" for dpkg ordering.
if [ -n "$1" ]; then
    VERSION="$1"
else
    DISP="$(sed -n 's/.*UG_VERSION "\(.*\)".*/\1/p' include/version.h)"
    VERSION="${DISP/-/\~}"
fi
echo "Building version: $VERSION"

[ -f ug-paneld ] || { echo "ug-paneld binary missing — run 'make' first" >&2; exit 1; }
gcc -O2 -Wall -o ug-fand src/ug_fand.c || { echo "ug-fand build failed" >&2; exit 1; }

build_tarball() {
    local platform="$1"
    local stage="/tmp/ug-paneld-tarball-$platform/ug-paneld"

    rm -rf "/tmp/ug-paneld-tarball-$platform"
    mkdir -p "$stage"

    cp ug-paneld ug-fand "$stage/"
    cp packaging/config.json.example "$stage/"
    cp packaging/fand/config.example "$stage/fand-config"
    cp "packaging/$platform/install.sh" "packaging/$platform/uninstall.sh" \
       "packaging/$platform/start.sh" "packaging/$platform/README.txt" "$stage/"
    chmod 755 "$stage/ug-paneld" "$stage/ug-fand" "$stage"/*.sh
    mkdir -p "$stage/wallpapers"
    cp packaging/wallpapers/*.png "$stage/wallpapers/"
    mkdir -p "$stage/web"
    cp web/* "$stage/web/"

    tar -C "/tmp/ug-paneld-tarball-$platform" --owner=0 --group=0 \
        -czf "$REPO/ug-paneld_${VERSION}_${platform}_amd64.tar.gz" ug-paneld
    rm -rf "/tmp/ug-paneld-tarball-$platform"
}

build_tarball truenas
build_tarball unraid

ls -la "$REPO"/ug-paneld_*_{truenas,unraid}_amd64.tar.gz
