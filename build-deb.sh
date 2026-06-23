#!/bin/bash
# Build ug-paneld .deb packages locally (mirrors .github/workflows/release.yml).
# Usage: ./build-deb.sh [version]   (run on Linux/WSL after `make`)
set -e

VERSION="${1:-1.0.1}"
REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

[ -f ug-paneld ] || { echo "ug-paneld binary missing — run 'make' first" >&2; exit 1; }
gcc -O2 -Wall -o ug-fand src/ug_fand.c || { echo "ug-fand build failed" >&2; exit 1; }

build_deb() {
    local suffix="$1"
    local blacklist="$2"
    local pkg="ug-paneld_${VERSION}${suffix}_amd64"
    local stage="/tmp/${pkg}"

    rm -rf "$stage"
    mkdir -p "$stage/DEBIAN" "$stage/usr/bin" "$stage/lib/systemd/system" \
             "$stage/etc/ug-paneld" "$stage/etc/ug-fand"

    cp ug-paneld ug-fand "$stage/usr/bin/"
    chmod 755 "$stage/usr/bin/ug-paneld" "$stage/usr/bin/ug-fand"
    cp ug-paneld.service packaging/fand/ug-fand.service "$stage/lib/systemd/system/"
    chmod 644 "$stage/lib/systemd/system/ug-paneld.service" \
              "$stage/lib/systemd/system/ug-fand.service"

    cp packaging/fand/config.example "$stage/etc/ug-fand/config"
    chmod 644 "$stage/etc/ug-fand/config"

    mkdir -p "$stage/usr/share/ug-paneld/wallpapers"
    cp packaging/wallpapers/*.png "$stage/usr/share/ug-paneld/wallpapers/"
    chmod 644 "$stage/usr/share/ug-paneld/wallpapers/"*.png

    # keep the user's fan config across upgrades
    printf "/etc/ug-fand/config\n" > "$stage/DEBIAN/conffiles"

    local desc_extra=""
    if [ "$blacklist" = "yes" ]; then
        mkdir -p "$stage/etc/modprobe.d"
        echo "blacklist i2c_hid_acpi" > "$stage/etc/modprobe.d/no-i2c-hid.conf"
        desc_extra=" Blacklists i2c-hid-acpi to free the touchscreen."
        printf "/etc/modprobe.d/no-i2c-hid.conf\n" >> "$stage/DEBIAN/conffiles"
    fi

    cat > "$stage/DEBIAN/control" <<CTRL
Package: ug-paneld
Version: ${VERSION}
Architecture: amd64
Maintainer: Sebastian Kowski
Depends: libdrm2, libcurl4
Description: Front panel display + fan control for Ugreen iDX6011 Pro NAS
 Drives the 258x960 display, backlight and touchscreen, and bundles ug-fand for
 fan monitoring and control, all from userspace.${desc_extra}
CTRL

    cat > "$stage/DEBIAN/postinst" <<'SCRIPT'
#!/bin/sh
systemctl daemon-reload
for s in ug-paneld ug-fand; do
    systemctl enable "$s" 2>/dev/null || true
    systemctl restart "$s" 2>/dev/null || true
done
echo "ug-paneld + ug-fand installed and (re)started."
SCRIPT
    chmod 755 "$stage/DEBIAN/postinst"

    cat > "$stage/DEBIAN/prerm" <<'SCRIPT'
#!/bin/sh
for s in ug-paneld ug-fand; do
    systemctl stop "$s" 2>/dev/null || true
    systemctl disable "$s" 2>/dev/null || true
done
SCRIPT
    chmod 755 "$stage/DEBIAN/prerm"

    dpkg-deb --build --root-owner-group "$stage" "$REPO/${pkg}.deb"
    rm -rf "$stage"
}

build_deb "" "yes"
build_deb "_no-blacklist" "no"

ls -la "$REPO"/ug-paneld_*.deb
