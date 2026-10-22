#!/bin/bash
# setup-ugreen-leds.sh — UGREEN iDX6011 (Pro): enable the front panel LEDs on Proxmox/Debian
#
# Background: without UGOS, the LED MCU (Holtek HT32F52231, I2C 0x3a on the SMBus I801
# adapter) runs an autonomous left-to-right rolling animation. This script takes over:
#   1) builds the CLI tool from klein0r's fork (iDX6011 Pro protocol; stops the animation)
#   2) installs the led-ugreen kernel module via DKMS
#      -> /sys/class/leds/{power,network_stat,network_stat2,disk1..6}
#   3) services: ugreen-probe-leds + ugreen-diskiomon (upstream) + ugreen-idx-netled (our
#      own LAN/power LED unit — the upstream netdevmon scripts only know the LED "netdev",
#      which does not exist on the iDX6011 Pro)
#
# Credits: https://github.com/klein0r/ugreen_leds_controller (fork with iDX6011 Pro support)
#          https://github.com/miskcoo/ugreen_leds_controller (original driver + scripts)
#          https://github.com/miskcoo/ugreen_leds_controller/issues/93 (protocol findings)
#          Bay wiring: noodlemctwoodle/unraid-ugreen-idx6011-pro (Pro), aoreskovic in
#          0x556c79/install_ugreen_leds_controller#23 and omgapuppy/unraid-ugreen-idx6011-led-fan (non-Pro)
#
# Run AS ROOT directly on the Proxmox/Debian host (not inside a VM/LXC):
#   bash setup-ugreen-leds.sh
#
# TrueNAS / Unraid: this script needs apt + DKMS and will refuse to run there. See the
# "Front panel LEDs" section of the repo README for what works on those platforms.
set -uo pipefail

REPO_URL="https://github.com/klein0r/ugreen_leds_controller"
SRC_DIR="/usr/local/src/ugreen_leds_controller"

log()  { echo -e "\n==> $*"; }
fail() { echo "ERROR: $*" >&2; exit 1; }

[[ $EUID -eq 0 ]] || fail "Please run as root."
command -v apt-get >/dev/null 2>&1 \
    || fail "No apt-get found. This script targets Proxmox/Debian; for TrueNAS/Unraid see the repo README."

product="$(cat /sys/class/dmi/id/product_name 2>/dev/null || true)"
log "DMI product_name: '${product:-<empty>}'  kernel: $(uname -r)"

# The non-Pro iDX6011 has the same LED MCU but only ONE LAN LED, so its bays
# start one register earlier: what the driver calls network_stat2/netdev2 is
# bay 1, disk1..5 are bays 2..6 and disk6 is not wired to anything.
case "$product" in
    *iDX6011*Pro*) LAYOUT=pro ;;
    *iDX6011*)     LAYOUT=nonpro ;;
    *)             LAYOUT=pro ;;
esac
echo "LED layout: $LAYOUT"

log "Installing packages"
apt-get update -qq || true
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    git build-essential i2c-tools smartmontools dkms || fail "Package installation failed."

log "Looking for kernel headers (needed by DKMS)"
HEADERS_OK=1
if [[ ! -d "/lib/modules/$(uname -r)/build" ]]; then
    apt-get install -y -qq "linux-headers-$(uname -r)" 2>/dev/null \
    || apt-get install -y -qq "proxmox-headers-$(uname -r)" 2>/dev/null \
    || apt-get install -y -qq "pve-headers-$(uname -r)" 2>/dev/null \
    || apt-get install -y -qq proxmox-default-headers 2>/dev/null \
    || apt-get install -y -qq pve-headers 2>/dev/null \
    || true
fi
# Also pull the header *meta* package so DKMS rebuilds the module AUTOMATICALLY
# on future kernel upgrades. The version-specific headers above only cover the
# kernel running right now; without this meta, the next Proxmox/Debian kernel
# update leaves DKMS with no headers and the LEDs silently stop after that
# reboot — the #1 cause of "LEDs aren't persistent after a kernel update".
log "Ensuring the kernel-header meta package (for automatic DKMS rebuilds)"
if apt-cache show proxmox-default-headers >/dev/null 2>&1; then
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq proxmox-default-headers || true  # Proxmox VE
else
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq linux-headers-amd64 || true       # plain Debian
fi
if [[ ! -d "/lib/modules/$(uname -r)/build" ]]; then
    HEADERS_OK=0
    echo "WARNING: No kernel headers found for $(uname -r)."
    echo "         Skipping the kernel module part; LEDs will be set statically via the CLI."
fi

modprobe -q i2c-i801 || true
modprobe i2c-dev || fail "Cannot load i2c-dev."

log "Looking for the LED controller (SMBus I801, address 0x3a)"
bus="$(i2cdetect -l | awk '/SMBus I801/ {sub("i2c-","",$1); print $1; exit}')"
[[ -n "${bus:-}" ]] || fail "No 'SMBus I801 adapter' found (check i2cdetect -l)."
echo "SMBus I801 = i2c-$bus"

log "Fetching sources ($REPO_URL)"
if [[ -d "$SRC_DIR/.git" ]]; then
    git -C "$SRC_DIR" checkout -- cli/ugreen_leds.cpp 2>/dev/null || true   # drop our patch before updating
    git -C "$SRC_DIR" pull --ff-only || true
else
    git clone --depth 1 "$REPO_URL" "$SRC_DIR" || fail "git clone failed."
fi

# The CLI's "is the MCU still animating?" probe wrote brightness 0 to register 0 --
# the power LED -- on every call, so any LED command switched the power LED off.
# Same patch as tools/patches/klein0r-cli-power-probe.patch (embedded: this script
# is downloaded on its own). If upstream changed that code, build it unpatched.
log "Patching the CLI (keeps the power LED on)"
cat > /tmp/ugreen-cli-power-probe.patch <<'PATCH'
diff --git a/cli/ugreen_leds.cpp b/cli/ugreen_leds.cpp
index 73c1d81..52df6b6 100644
--- a/cli/ugreen_leds.cpp
+++ b/cli/ugreen_leds.cpp
@@ -37,11 +37,25 @@ ugreen_leds_t::model_t ugreen_leds_t::detect_model() {
 // Detected by sending a no-op write (set brightness to current value) to reg 0 and checking
 // the MCU's acknowledgement register 0x80. In animation mode the MCU ignores host writes,
 // so 0x80 stays != 1.
+//
+// ugreen-idx6011-panel: reg 0 is the power LED, and the probe used to send brightness 0
+// rather than the current value, so every CLI call switched the power LED off. Read the
+// current brightness first (status block at 0x81 + reg) and write exactly that back. If
+// the status cannot be read, the MCU is most likely still animating and ignores the write
+// anyway, so falling back to 0 changes nothing.
+static bool verify_checksum(const std::vector<uint8_t>& data);
+
 bool ugreen_leds_t::needs_init_idx6011() {
-    uint16_t sum = 0xa1 + 0x01; // cmd=0x01, all params=0
+    uint8_t cur = 0;
+    for (int i = 0; i < 3; i++) {
+        auto st = _i2c.read_block_data(0x81, 0xb);
+        if (st.size() == 0xb && verify_checksum(st) && st[0] <= 3) { cur = st[1]; break; }
+        std::this_thread::sleep_for(std::chrono::milliseconds(5));
+    }
+    uint16_t sum = 0xa1 + 0x01 + cur; // cmd=0x01 (brightness), p1=current brightness
     std::vector<uint8_t> probe {
         0xa0, 0x01, 0x00, 0x00,
-        0x01, 0x00, 0x00, 0x00, 0x00,
+        0x01, cur, 0x00, 0x00, 0x00,
         (uint8_t)((sum >> 8) & 0xff),
         (uint8_t)(sum & 0xff)
     };
PATCH
if git -C "$SRC_DIR" apply --check /tmp/ugreen-cli-power-probe.patch 2>/dev/null; then
    git -C "$SRC_DIR" apply /tmp/ugreen-cli-power-probe.patch && echo "applied"
else
    echo "WARNING: the CLI patch does not apply (upstream changed?) - building unpatched; the power LED may go dark"
fi
rm -f /tmp/ugreen-cli-power-probe.patch

log "Building and installing the CLI tool"
make -C "$SRC_DIR/cli" -j"$(nproc)" || fail "CLI build failed."
install -m 755 "$SRC_DIR/cli/ugreen_leds_cli" /usr/local/bin/ugreen_leds_cli

# On a re-run (e.g. to repair after a kernel update) the kernel module still
# holds the LED controller, and the CLI cannot open it next to the driver.
# Release it; the module is reloaded and the services restarted further down.
if grep -q '^led_ugreen ' /proc/modules; then   # not lsmod|grep -q: pipefail + SIGPIPE
    log "Releasing the LED controller from the kernel module (re-run)"
    systemctl stop ugreen-diskiomon.service ugreen-idx-netled.service 2>/dev/null || true
    rmmod led_ugreen || fail "Cannot unload led_ugreen (in use?)."
fi

log "Taking over the MCU (stops the rolling animation) + reading status"
export UGREEN_MODEL=idx6011
ugreen_leds_cli all -status || fail "LED controller not responding (0x3a on i2c-$bus)."

# Sane default state, in case the kernel module part is skipped or fails below
if [[ $LAYOUT == nonpro ]]; then
    STATIC_LEDS='ugreen_leds_cli power -on -color 255 255 255 -brightness 144
ugreen_leds_cli netdev -on -color 255 255 255 -brightness 96
ugreen_leds_cli netdev2 disk1 disk2 disk3 disk4 disk5 -on -color 255 255 255 -brightness 64
ugreen_leds_cli disk6 -off'
else
    STATIC_LEDS='ugreen_leds_cli power -on -color 255 255 255 -brightness 144
ugreen_leds_cli netdev netdev2 -on -color 255 255 255 -brightness 96
ugreen_leds_cli disk1 disk2 disk3 disk4 disk5 disk6 -on -color 255 255 255 -brightness 64'
fi
eval "$STATIC_LEDS"

if [[ $HEADERS_OK -eq 1 && "$product" == *iDX6011* ]]; then
    log "Installing the led-ugreen kernel module via DKMS"
    DKMS_VER="$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' "$SRC_DIR/kmod/dkms.conf")"
    DKMS_VER="${DKMS_VER:-0.3}"
    rm -rf "/usr/src/led-ugreen-$DKMS_VER"
    cp -r "$SRC_DIR/kmod" "/usr/src/led-ugreen-$DKMS_VER"
    dkms remove -m led-ugreen -v "$DKMS_VER" --all >/dev/null 2>&1 || true
    dkms add    -m led-ugreen -v "$DKMS_VER" || fail "dkms add failed."
    dkms build  -m led-ugreen -v "$DKMS_VER" || fail "dkms build failed (check kernel headers)."
    dkms install -m led-ugreen -v "$DKMS_VER" || fail "dkms install failed."

    log "Loading modules at boot"
    cat > /etc/modules-load.d/ugreen-led.conf <<'EOF'
i2c-dev
led-ugreen
ledtrig-oneshot
ledtrig-netdev
EOF

    log "Installing scripts + services"
    install -m 755 "$SRC_DIR/scripts/ugreen-probe-leds"   /usr/bin/ugreen-probe-leds
    install -m 755 "$SRC_DIR/scripts/ugreen-diskiomon"    /usr/bin/ugreen-diskiomon
    # Upstream diskiomon has no iDX6011 entry, so teach it the bay wiring. The
    # bays hang off an ASMedia ASM1164 (4 ports) and the Intel chipset (2):
    #   Pro      ASMedia -> bays 1 2 5 6, Intel -> bays 3 4
    #   non-Pro  ASMedia -> bays 1 2 3 4, Intel -> bays 5 6
    # The ports are looked up at every start by PCI vendor, not hardcoded as
    # ataN, because the ASMedia's bus address moves between boots and BIOS
    # versions. The non-Pro also has only one LAN LED, so its bay LEDs start
    # at network_stat2.
    cat > /tmp/ugreen-diskiomon-idx.$$ <<'EOF'
        iDX6011*)   # added by setup-ugreen-leds.sh
            echo "Found UGREEN ${product_name}"
            if [[ "${product_name}" == *Pro* ]]; then
                _idx_asm=(1 2 5 6); _idx_intel=(3 4)
            else
                _idx_asm=(1 2 3 4); _idx_intel=(5 6)
                led_map=(network_stat2 disk1 disk2 disk3 disk4 disk5)
            fi
            _idx_ata=()
            _idx_seen=""
            for _idx_dev in /sys/bus/pci/devices/*; do
                _idx_ports=$(ls -1 "$_idx_dev" 2>/dev/null | grep -xE 'ata[0-9]+' | sort -V)
                [[ -n "$_idx_ports" ]] || continue
                case "$(cat "$_idx_dev/vendor" 2>/dev/null)" in
                    0x1b21) _idx_v=asm;   _idx_bays=("${_idx_asm[@]}") ;;
                    0x8086) _idx_v=intel; _idx_bays=("${_idx_intel[@]}") ;;
                    *) continue ;;
                esac
                [[ "$_idx_seen" == *"$_idx_v"* ]] && continue
                _idx_seen+=" $_idx_v"
                _idx_i=0
                for _idx_p in $_idx_ports; do
                    (( _idx_i < ${#_idx_bays[@]} )) || break
                    _idx_ata[${_idx_bays[$_idx_i]} - 1]=$_idx_p
                    _idx_i=$((_idx_i + 1))
                done
            done
            if (( ${#_idx_ata[@]} > 0 )); then
                ata_map=()
                hctl_map=()
                for _idx_i in 0 1 2 3 4 5; do
                    _idx_p=${_idx_ata[$_idx_i]:-none}
                    ata_map+=("$_idx_p")
                    if [[ $_idx_p == none ]]; then hctl_map+=(none); else hctl_map+=("$(( ${_idx_p#ata} - 1 )):0:0:0"); fi
                done
                echo "iDX6011 bay ports: ${ata_map[*]}"
            fi
            ;;
EOF
    sed -i '/case "\${product_name}" in/r /tmp/ugreen-diskiomon-idx.'$$ /usr/bin/ugreen-diskiomon
    rm -f /tmp/ugreen-diskiomon-idx.$$
    grep -qF 'iDX6011*)   # added by setup-ugreen-leds.sh' /usr/bin/ugreen-diskiomon \
        || echo "WARNING: could not add the iDX6011 bay map to ugreen-diskiomon (upstream changed?)"
    install -m 755 "$SRC_DIR/scripts/ugreen-detect-disks" /usr/bin/ugreen-detect-disks
    [[ -f /etc/ugreen-leds.conf ]] || install -m 644 "$SRC_DIR/scripts/ugreen-leds.conf" /etc/ugreen-leds.conf
    grep -q '^NETDEV_COLOR_BY_SPEED=' /etc/ugreen-leds.conf || cat >> /etc/ugreen-leds.conf <<'EOF'

# ---- added by setup-ugreen-leds.sh (iDX6011, read by ugreen-idx-netled) ----
# 1 = each LAN LED shows its negotiated link speed instead of COLOR_NETDEV_NORMAL
# (set when the LED setup starts: boot, or LEDs switched on in the panel)
NETDEV_COLOR_BY_SPEED=0
COLOR_NIC_10G="0 255 0"         # green
COLOR_NIC_5G="255 0 255"        # magenta
COLOR_NIC_2G5="255 90 0"        # orange
COLOR_NIC_1G="255 200 0"        # yellow
COLOR_NIC_OTHER="255 255 255"   # 100M and anything else
EOF
    install -m 644 "$SRC_DIR/scripts/systemd/ugreen-probe-leds.service" /etc/systemd/system/
    install -m 644 "$SRC_DIR/scripts/systemd/ugreen-diskiomon.service"  /etc/systemd/system/

    # ugreen-diskiomon maps disks to bay LEDs by device name, once, at start. A
    # pulled, swapped or re-inserted drive comes back under a new name, so re-read
    # the bays whenever a SATA disk comes or goes. The rescan unit waits a few
    # seconds first, so a burst of events (several drives, or one drive settling)
    # ends in a single restart; try-restart leaves a stopped monitor alone, so it
    # never lights the LEDs while the panel has them off (night mode).
    cat > /etc/systemd/system/ugreen-diskiomon-rescan.service <<'EOF'
[Unit]
Description=UGREEN iDX6011: re-read the disk bays after a drive was added or removed

[Service]
Type=oneshot
ExecStartPre=/bin/sleep 5
ExecStart=/usr/bin/systemctl try-restart ugreen-diskiomon.service
EOF
    cat > /etc/udev/rules.d/99-ugreen-diskiomon-rescan.rules <<'EOF'
ACTION=="add|remove", SUBSYSTEM=="block", ENV{DEVTYPE}=="disk", KERNEL=="sd[a-z]*", RUN+="/usr/bin/systemctl --no-block start ugreen-diskiomon-rescan.service"
EOF
    udevadm control --reload || true

    # Our own LAN/power LED unit: the upstream netdevmon variants hardcode the LED name
    # "netdev"; the iDX module registers network_stat + network_stat2 instead.
    cat > /usr/local/bin/ugreen-idx-netled <<'EOF'
#!/bin/bash
# Binds the LAN LEDs (two on the iDX6011 Pro, one on the iDX6011) to the physical NICs
# via the netdev trigger and sets the power LED. Override the NIC order via
# /etc/default/ugreen-idx-netled:
#   NETLED_IFACES="enp2s0 enp3s0"
# Colors come from /etc/ugreen-leds.conf (COLOR_NETDEV_NORMAL, COLOR_POWER — the same
# keys the panel's web dashboard edits). NETDEV_COLOR_BY_SPEED=1 there colors each LAN
# LED by its negotiated link speed instead (COLOR_NIC_*); this is applied whenever the
# LED setup starts (boot, LEDs switched on), as Linux sends no event on a speed change.
NETLED_IFACES=""
COLOR_NETDEV_NORMAL="255 255 255"
COLOR_POWER="255 255 255"
NETDEV_COLOR_BY_SPEED=0
COLOR_NIC_10G="0 255 0"
COLOR_NIC_5G="255 0 255"
COLOR_NIC_2G5="255 90 0"
COLOR_NIC_1G="255 200 0"
COLOR_NIC_OTHER="255 255 255"
[[ -f /etc/ugreen-leds.conf ]] && . /etc/ugreen-leds.conf
[[ -f /etc/default/ugreen-idx-netled ]] && . /etc/default/ugreen-idx-netled

nic_color() {
    [[ "$NETDEV_COLOR_BY_SPEED" == 1 ]] || { echo "$COLOR_NETDEV_NORMAL"; return; }
    case "$(cat "/sys/class/net/$1/speed" 2>/dev/null)" in
        10000) echo "$COLOR_NIC_10G" ;;
        5000)  echo "$COLOR_NIC_5G" ;;
        2500)  echo "$COLOR_NIC_2G5" ;;
        1000)  echo "$COLOR_NIC_1G" ;;
        *)     echo "$COLOR_NIC_OTHER" ;;
    esac
}

if [[ -z "$NETLED_IFACES" ]]; then
    NETLED_IFACES="$(for n in /sys/class/net/*; do
        [[ -e "$n/device" ]] && basename "$n"
    done | sort | head -n 2 | tr '\n' ' ')"
fi
read -r -a nics <<< "$NETLED_IFACES"

leds=(network_stat network_stat2)
case "$(cat /sys/class/dmi/id/product_name 2>/dev/null)" in
    *iDX6011*Pro*) ;;
    *iDX6011*)
        # non-Pro: a single LAN LED (network_stat2 is disk bay 1 there and
        # belongs to ugreen-diskiomon). It follows the first port with a link.
        leds=(network_stat)
        for n in "${nics[@]}"; do
            if [[ "$(cat "/sys/class/net/$n/carrier" 2>/dev/null)" == 1 ]]; then
                nics=("$n")
                break
            fi
        done
        ;;
esac
for i in "${!leds[@]}"; do
    led="/sys/class/leds/${leds[$i]}"
    [[ -d "$led" ]] || continue
    nic="${nics[$i]:-}"
    if [[ -n "$nic" ]]; then
        # color and brightness first: the netdev trigger then switches the LED
        # by the link state itself (a brightness written after it would light
        # a port that has no link)
        echo none > "$led/trigger"
        nic_color "$nic" > "$led/color" 2>/dev/null
        echo 160 > "$led/brightness"
        echo netdev  > "$led/trigger"
        echo "$nic"  > "$led/device_name"
        echo 1 > "$led/link"; echo 1 > "$led/tx"; echo 1 > "$led/rx"
    else
        echo none > "$led/trigger"
        echo 0    > "$led/brightness"
    fi
done

if [[ -d /sys/class/leds/power ]]; then
    echo none > /sys/class/leds/power/trigger
    echo "$COLOR_POWER" > /sys/class/leds/power/color 2>/dev/null
    echo 160 > /sys/class/leds/power/brightness
fi
exit 0
EOF
    chmod 755 /usr/local/bin/ugreen-idx-netled

    cat > /etc/systemd/system/ugreen-idx-netled.service <<'EOF'
[Unit]
Description=UGREEN iDX6011: LAN/Power LED setup (netdev trigger)
After=ugreen-probe-leds.service
Requires=ugreen-probe-leds.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/ugreen-idx-netled
RemainAfterExit=true
StandardOutput=journal

[Install]
WantedBy=multi-user.target
EOF

    log "Loading modules and starting services"
    modprobe ledtrig-oneshot || true
    modprobe ledtrig-netdev  || true
    modprobe led-ugreen      || fail "Cannot load led-ugreen."
    systemctl daemon-reload
    systemctl disable --now ugreen-leds-static.service >/dev/null 2>&1 || true
    systemctl enable --now ugreen-probe-leds.service  || fail "ugreen-probe-leds failed."
    systemctl enable --now ugreen-idx-netled.service
    systemctl enable --now ugreen-diskiomon.service

    log "Result"
    ls /sys/class/leds/ 2>/dev/null
    echo
    echo "DONE. Power/LAN/disk LEDs are now driven by the kernel module:"
    echo "  - Disk activity/health: ugreen-diskiomon (config: /etc/ugreen-leds.conf)"
    echo "  - LAN LEDs blink on traffic (ugreen-idx-netled; fix the port order in"
    echo "    /etc/default/ugreen-idx-netled if needed: NETLED_IFACES=\"...\")"
    echo
    echo "NOTE on bay mapping: bays are matched to SATA ports by controller"
    echo "  (journalctl -u ugreen-diskiomon shows 'iDX6011 bay ports: ...')."
    echo "  Verify: generate I/O on one disk and check the right bay blinks."
    echo "  If swapped: run 'ugreen-detect-disks' and switch /etc/ugreen-leds.conf"
    echo "  to MAPPING_METHOD=serial with your serial list."
    echo
    echo "NOTE on the CLI: only use ugreen_leds_cli with the module unloaded"
    echo "  (systemctl stop ugreen-diskiomon ugreen-idx-netled; rmmod led_ugreen)."
else
    log "Kernel module skipped — installing static boot service (CLI variant)"
    cat > /usr/local/bin/ugreen-leds-static <<EOF
#!/bin/bash
# Stops the rolling animation at boot and sets a static LED state (CLI variant,
# $LAYOUT front panel).
export UGREEN_MODEL=idx6011
export PATH=/usr/local/bin:\$PATH
$STATIC_LEDS
EOF
    chmod 755 /usr/local/bin/ugreen-leds-static

    cat > /etc/systemd/system/ugreen-leds-static.service <<'EOF'
[Unit]
Description=UGREEN iDX6011: static front LEDs (CLI, stops rolling animation)
After=systemd-modules-load.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/ugreen-leds-static
RemainAfterExit=true
StandardOutput=journal

[Install]
WantedBy=multi-user.target
EOF
    echo "i2c-dev" > /etc/modules-load.d/ugreen-led.conf
    systemctl daemon-reload
    systemctl enable --now ugreen-leds-static.service

    echo
    if [[ $HEADERS_OK -eq 0 ]]; then
        echo "DONE (static). For live status LEDs (disk/network activity), install"
        echo "kernel headers and re-run this script."
    else
        echo "DONE (static). DMI product_name does not contain 'iDX6011' — please report"
        echo "the output of 'cat /sys/class/dmi/id/product_name' so we can add support."
    fi
fi
