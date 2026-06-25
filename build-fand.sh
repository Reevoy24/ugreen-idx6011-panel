#!/bin/bash
# Build the display-free ug-fand (fan control ONLY) release artifacts for the
# UGREEN iDX6011/iDX6012 (Pro + non-Pro, auto-detected). No ug-paneld, no web,
# no wallpapers — these boxes (esp. the non-Pro) have no display.
#
#   ug-fand_<ver>_amd64.deb            Proxmox / Debian (systemd)
#   ug-fand_<ver>_truenas_amd64.tar.gz TrueNAS SCALE (pool + Post-Init)
#   ug-fand_<ver>_unraid_amd64.tar.gz  Unraid (/boot/config + go hook)
#
# Usage: ./build-fand.sh [version]   (run on Linux/WSL)
set -e
VERSION="${1:-1.0.0}"
REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"
FAND=packaging/fand

# Strip CR from staged text files — when built in WSL against a Windows working
# tree (autocrlf), scripts/config can be CRLF, which breaks #!/bin/sh on Linux.
norm() { sed -i 's/\r$//' "$@"; }

# Static (libc only) so one binary runs across Proxmox/TrueNAS/Unraid glibc versions.
gcc -O2 -g0 -static -Wall -Wextra -Iinclude -o ug-fand src/ug_fand.c
strip ug-fand
echo "Built static ug-fand ($(du -h ug-fand | cut -f1))"

# ---------- Proxmox / Debian .deb ----------
build_deb() {
    local pkg="ug-fand_${VERSION}_amd64"
    local stage="/tmp/$pkg"
    rm -rf "$stage"
    mkdir -p "$stage/DEBIAN" "$stage/usr/bin" "$stage/lib/systemd/system" "$stage/etc/ug-fand"

    cp ug-fand "$stage/usr/bin/ug-fand";                       chmod 755 "$stage/usr/bin/ug-fand"
    cp "$FAND/ug-fand.service" "$stage/lib/systemd/system/";   chmod 644 "$stage/lib/systemd/system/ug-fand.service"
    cp "$FAND/config.example" "$stage/etc/ug-fand/config";     chmod 644 "$stage/etc/ug-fand/config"
    norm "$stage/lib/systemd/system/ug-fand.service" "$stage/etc/ug-fand/config"
    printf "/etc/ug-fand/config\n" > "$stage/DEBIAN/conffiles"   # keep user's config across upgrades

    cat > "$stage/DEBIAN/control" <<CTRL
Package: ug-fand
Version: ${VERSION}
Architecture: amd64
Maintainer: Sebastian Kowski
Description: Fan monitor + control for UGREEN iDX6011/iDX6012 NAS (no display)
 Userspace fan monitoring and control for the iDX6011/iDX6012 (Pro and non-Pro,
 auto-detected by DMI) via the ITE IT55xx embedded controller. Display-free.
CTRL

    cat > "$stage/DEBIAN/postinst" <<'SCRIPT'
#!/bin/sh
systemctl daemon-reload
systemctl enable ug-fand 2>/dev/null || true
systemctl restart ug-fand 2>/dev/null || true
echo "ug-fand installed and (re)started."
SCRIPT
    chmod 755 "$stage/DEBIAN/postinst"

    cat > "$stage/DEBIAN/prerm" <<'SCRIPT'
#!/bin/sh
systemctl stop ug-fand 2>/dev/null || true
systemctl disable ug-fand 2>/dev/null || true
SCRIPT
    chmod 755 "$stage/DEBIAN/prerm"

    dpkg-deb --build --root-owner-group "$stage" "$REPO/${pkg}.deb"
    rm -rf "$stage"
}

# ---------- TrueNAS / Unraid tarball (binary + scripts + config, no display) ----------
build_tarball() {
    local platform="$1"
    local root="/tmp/ug-fand-tarball-$platform"
    local stage="$root/ug-fand"
    rm -rf "$root"
    mkdir -p "$stage"
    cp ug-fand "$stage/ug-fand"
    cp "$FAND/config.example" "$stage/config"     # active config (the source of truth)
    cp "$FAND/install.sh" "$FAND/start.sh" "$FAND/README.txt" "$stage/"
    norm "$stage/install.sh" "$stage/start.sh" "$stage/config" "$stage/README.txt"
    chmod 755 "$stage/ug-fand" "$stage"/*.sh
    tar -C "$root" --owner=0 --group=0 -czf "$REPO/ug-fand_${VERSION}_${platform}_amd64.tar.gz" ug-fand
    rm -rf "$root"
}

build_deb
build_tarball truenas
build_tarball unraid

echo
ls -la "$REPO"/ug-fand_${VERSION}_amd64.deb "$REPO"/ug-fand_${VERSION}_*_amd64.tar.gz
