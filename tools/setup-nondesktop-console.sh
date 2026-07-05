#!/bin/bash
# setup-nondesktop-console.sh — UGREEN iDX6011 Pro (Proxmox/Debian)
#
# Problem: with the front eDP panel (258x960) AND an external HDMI/DP monitor
# connected, the kernel's text console (fbcon) sizes its framebuffer to the
# SMALLEST display, so the big monitor only shows a tiny 258x960 console in the
# corner. This is not ug-paneld's doing — the panel and the monitor sit on
# separate display pipes; it is how the in-kernel fbdev console clones.
#
# Fix: mark the front panel as a "non-desktop" display via an EDID override —
# the same flag the kernel uses for VR headsets and the Apple Touch Bar. The
# console then IGNORES the panel and sizes itself to the external monitor alone
# (full resolution). ug-paneld is unaffected: it selects the connector directly
# and never reads the EDID identity, so a non-desktop hint changes nothing for it.
#
# The override EDID is a byte-for-byte copy of THIS unit's real panel EDID with a
# Microsoft "specialized monitor" vendor block appended (no VR-vendor spoofing).
#
# Fail-safe: if the file is ever missing/unreadable the kernel falls back to the
# real EDID (today's behavior) — never a brick. Reversible with --uninstall.
#
# CAVEAT: with the panel flagged non-desktop and NO external monitor attached at
# boot, there is no local text console at all until a monitor is plugged in
# (the front panel via ug-paneld and SSH keep working). Fine for a NAS.
#
# Usage (run AS ROOT on the Proxmox/Debian host, not in a VM/LXC):
#   bash setup-nondesktop-console.sh              # install (then reboot)
#   bash setup-nondesktop-console.sh --uninstall  # revert  (then reboot)
set -uo pipefail

FW_DIR="/lib/firmware/edid"
FW_FILE="$FW_DIR/ugreen-panel.bin"
HOOK="/etc/initramfs-tools/hooks/edid-ugreen"

log()  { echo -e "\n==> $*"; }
fail() { echo "ERROR: $*" >&2; exit 1; }

[[ $EUID -eq 0 ]] || fail "Please run as root."
command -v update-initramfs >/dev/null 2>&1 \
    || fail "No initramfs-tools found. This script targets Proxmox/Debian."

# Locate the front panel connector (card number varies between boots/units).
EDP="$(ls -d /sys/class/drm/card*-eDP-1 2>/dev/null | head -1)"
[[ -n "$EDP" ]] || fail "No eDP-1 connector found (is the panel up? check ls /sys/class/drm/)."
CONN="$(basename "$EDP" | sed 's/^card[0-9]*-//')"   # e.g. eDP-1
CMDARG="drm.edid_firmware=$CONN:edid/ugreen-panel.bin"

# --- which bootloader carries the kernel command line? ---------------------
# proxmox-boot-tool (systemd-boot) -> /etc/kernel/cmdline; otherwise GRUB.
if proxmox-boot-tool status >/dev/null 2>&1 && [[ -e /etc/kernel/cmdline ]]; then
    BOOTLOADER="systemd-boot"
else
    BOOTLOADER="grub"
fi

add_cmdline() {
    if [[ "$BOOTLOADER" == systemd-boot ]]; then
        grep -qF "$CMDARG" /etc/kernel/cmdline || sed -i "s#\$# $CMDARG#" /etc/kernel/cmdline
        proxmox-boot-tool refresh
    else
        cp -n /etc/default/grub /etc/default/grub.ugbak 2>/dev/null || true
        grep -qF "$CMDARG" /etc/default/grub \
            || sed -i "s#\(GRUB_CMDLINE_LINUX_DEFAULT=\"[^\"]*\)\"#\1 $CMDARG\"#" /etc/default/grub
        update-grub
    fi
}

del_cmdline() {
    if [[ "$BOOTLOADER" == systemd-boot ]]; then
        sed -i "s# $CMDARG##g" /etc/kernel/cmdline
        proxmox-boot-tool refresh
    else
        sed -i "s# $CMDARG##g" /etc/default/grub
        update-grub
    fi
}

# --------------------------------------------------------------------------- #
if [[ "${1:-}" == "--uninstall" ]]; then
    log "Reverting non-desktop console override ($BOOTLOADER)"
    del_cmdline
    rm -f "$HOOK" "$FW_FILE"
    update-initramfs -u -k all
    echo
    echo "DONE. Reboot to apply:  reboot"
    exit 0
fi

# --- install --------------------------------------------------------------- #
log "Front panel: $CONN on $(basename "$(dirname "$EDP")")   bootloader: $BOOTLOADER"

sz="$(wc -c < "$EDP/edid" 2>/dev/null || echo 0)"
[[ "$sz" == "256" ]] || fail "eDP EDID is $sz bytes, expected 256. If a previous override is already active (384 bytes), run --uninstall and reboot first."

mkdir -p "$FW_DIR"
cp "$EDP/edid" "$FW_FILE.orig"     # keep the untouched original as a backup

log "Generating the non-desktop override EDID"
python3 - "$EDP/edid" "$FW_FILE" <<'PY' || fail "EDID generation failed."
import sys
src, dst = sys.argv[1], sys.argv[2]
edid = bytearray(open(src, "rb").read())
if len(edid) != 256:
    sys.exit("unexpected EDID length %d" % len(edid))
if edid[126] != 1:
    sys.exit("unexpected extension count %d (expected 1)" % edid[126])
block0, block1 = bytearray(edid[0:128]), bytearray(edid[128:256])
def cksum(b): b[127] = (256 - (sum(b[0:127]) % 256)) % 256
# base EDID: extension count 1 -> 2 (we add one CTA block), re-checksum
block0[126] = 2; cksum(block0)
# CTA-861 extension carrying the full Microsoft "specialized monitor" VSDB:
# header 0x75 (payload 21) | OUI 5c 12 ca | version 2 -> non-desktop | flags |
# 16-byte zeroed container ID.
block2 = bytearray(128); block2[0] = 0x02; block2[1] = 0x03
vsdb = bytes([0x60 | 21, 0x5c, 0x12, 0xca, 0x02, 0x00]) + bytes(16)
block2[4:4 + len(vsdb)] = vsdb
block2[2] = 4 + len(vsdb)   # 'd': DTDs would start here (none)
block2[3] = 0x00
cksum(block2)
open(dst, "wb").write(bytes(block0) + bytes(block1) + bytes(block2))
print("wrote %s (%d bytes)" % (dst, 384))
PY

if command -v edid-decode >/dev/null 2>&1; then
    edid-decode < "$FW_FILE" 2>/dev/null | grep -iE 'extension blocks|Microsoft|258x960' | sed 's/^/    /'
fi

log "Making the override load at boot ($BOOTLOADER + initramfs)"
add_cmdline

# i915 loads early (initramfs) and reads the EDID there — the file MUST be in the
# initramfs or the override is silently ignored (initramfs-tools does not bundle
# /lib/firmware/edid/* by default).
cat > "$HOOK" <<EOF
#!/bin/sh
[ "\$1" = prereqs ] && { echo; exit 0; }
. /usr/share/initramfs-tools/hook-functions
mkdir -p "\${DESTDIR}${FW_DIR}"
cp "$FW_FILE" "\${DESTDIR}${FW_DIR}/"
EOF
chmod +x "$HOOK"
update-initramfs -u -k all

echo
echo "DONE. Now reboot (as a SEPARATE command — not chained with &&):"
echo "    reboot"
echo
echo "After the reboot, verify:"
echo "    modetest -M i915 -c 2>/dev/null | grep -A60 'connected.*eDP-1' | grep -A3 -m1 non-desktop"
echo "  -> should show  value: 1   (panel flagged non-desktop)"
echo "The external monitor's console then uses its full resolution."
