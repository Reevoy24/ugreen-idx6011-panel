#!/bin/bash
# Build the display-free ug-fand (fan control ONLY) release artifacts for the
# UGREEN iDX6011 (Pro + non-Pro, auto-detected). No ug-paneld, no web,
# no wallpapers — these units (esp. the non-Pro) have no display.
#
#   ug-fand_<ver>_amd64.deb            Proxmox / Debian (systemd)
#   ug-fand_<ver>_truenas_amd64.tar.gz TrueNAS SCALE (pool + Post-Init)
#   ug-fand_<ver>_unraid_amd64.tar.gz  Unraid (/boot/config + go hook)
#
# Usage: ./build-fand.sh [version]   (run on Linux/WSL)
set -e
REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"
FAND=packaging/fand

# Version: an explicit arg wins; otherwise derive it from include/version.h
# (UG_FAND_VERSION — the single source shown in the web footer), turning a
# "-betaN" suffix into "~betaN" so dpkg orders the beta before the release.
if [ -n "$1" ]; then
    VERSION="$1"
else
    DISP="$(sed -n 's/.*UG_FAND_VERSION "\(.*\)".*/\1/p' include/version.h)"
    VERSION="${DISP/-/\~}"
fi
echo "Building ug-fand version: $VERSION"

# Strip CR from staged text files — when built in WSL against a Windows working
# tree (autocrlf), scripts/config can be CRLF, which breaks #!/bin/sh on Linux.
norm() { sed -i 's/\r$//' "$@"; }

# Static (libc only) so one binary runs across Proxmox/TrueNAS/Unraid glibc versions.
# fand_api.c + the shared stat collectors add the optional web dashboard; pthread
# for the API thread. No external libs, so the static link stays clean.
gcc -O2 -g0 -static -Wall -Wextra -Iinclude -pthread -o ug-fand \
    src/ug_fand.c src/fand_api.c src/system_stats.c src/net_stats.c src/disk_stats.c
strip ug-fand
echo "Built static ug-fand ($(du -h ug-fand | cut -f1))"

# ---------- Proxmox / Debian .deb ----------
build_deb() {
    local pkg="ug-fand_${VERSION}_amd64"
    local stage="/tmp/$pkg"
    rm -rf "$stage"
    mkdir -p "$stage/DEBIAN" "$stage/usr/bin" "$stage/lib/systemd/system" \
             "$stage/etc/ug-fand" "$stage/usr/share/ug-fand/web"

    cp ug-fand "$stage/usr/bin/ug-fand";                       chmod 755 "$stage/usr/bin/ug-fand"
    cp "$FAND/ug-fand.service" "$stage/lib/systemd/system/";   chmod 644 "$stage/lib/systemd/system/ug-fand.service"
    cp "$FAND/config.example" "$stage/etc/ug-fand/config";     chmod 644 "$stage/etc/ug-fand/config"
    # Web dashboard frontend (served when api_port is set; default UG_FAND_WEB_DIR).
    cp web/* "$stage/usr/share/ug-fand/web/";                  chmod 644 "$stage/usr/share/ug-fand/web/"*
    norm "$stage/lib/systemd/system/ug-fand.service" "$stage/etc/ug-fand/config"
    printf "/etc/ug-fand/config\n" > "$stage/DEBIAN/conffiles"   # keep user's config across upgrades

    cat > "$stage/DEBIAN/control" <<CTRL
Package: ug-fand
Version: ${VERSION}
Architecture: amd64
Maintainer: Sebastian Kowski
Description: Fan monitor + control for UGREEN iDX6011 NAS (no display)
 Userspace fan monitoring and control for the iDX6011 (Pro and non-Pro,
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
    mkdir -p "$stage/web"                          # web dashboard (start.sh points UG_FAND_WEB_DIR here)
    cp web/* "$stage/web/"
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
