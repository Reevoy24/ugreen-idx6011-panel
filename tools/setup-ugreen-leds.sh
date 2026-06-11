#!/bin/bash
# setup-ugreen-leds.sh — UGREEN iDX6011 Pro: enable the front panel LEDs on Proxmox/Debian
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
    git -C "$SRC_DIR" pull --ff-only || true
else
    git clone --depth 1 "$REPO_URL" "$SRC_DIR" || fail "git clone failed."
fi

log "Building and installing the CLI tool"
make -C "$SRC_DIR/cli" -j"$(nproc)" || fail "CLI build failed."
install -m 755 "$SRC_DIR/cli/ugreen_leds_cli" /usr/local/bin/ugreen_leds_cli

log "Taking over the MCU (stops the rolling animation) + reading status"
export UGREEN_MODEL=idx6011
ugreen_leds_cli all -status || fail "LED controller not responding (0x3a on i2c-$bus)."

# Sane default state, in case the kernel module part is skipped or fails below
ugreen_leds_cli power -on -color 255 255 255 -brightness 144
ugreen_leds_cli netdev netdev2 -on -color 255 255 255 -brightness 96
ugreen_leds_cli disk1 disk2 disk3 disk4 disk5 disk6 -on -color 255 255 255 -brightness 64

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
    install -m 755 "$SRC_DIR/scripts/ugreen-detect-disks" /usr/bin/ugreen-detect-disks
    [[ -f /etc/ugreen-leds.conf ]] || install -m 644 "$SRC_DIR/scripts/ugreen-leds.conf" /etc/ugreen-leds.conf
    install -m 644 "$SRC_DIR/scripts/systemd/ugreen-probe-leds.service" /etc/systemd/system/
    install -m 644 "$SRC_DIR/scripts/systemd/ugreen-diskiomon.service"  /etc/systemd/system/

    # Our own LAN/power LED unit: the upstream netdevmon variants hardcode the LED name
    # "netdev"; the iDX module registers network_stat + network_stat2 instead.
    cat > /usr/local/bin/ugreen-idx-netled <<'EOF'
#!/bin/bash
# Binds the two LAN LEDs of the iDX6011 Pro to the physical NICs via the netdev trigger
# and sets the power LED. Override the NIC order via /etc/default/ugreen-idx-netled:
#   NETLED_IFACES="enp2s0 enp3s0"
NETLED_IFACES=""
[[ -f /etc/default/ugreen-idx-netled ]] && . /etc/default/ugreen-idx-netled

if [[ -z "$NETLED_IFACES" ]]; then
    NETLED_IFACES="$(for n in /sys/class/net/*; do
        [[ -e "$n/device" ]] && basename "$n"
    done | sort | head -n 2 | tr '\n' ' ')"
fi
read -r -a nics <<< "$NETLED_IFACES"

leds=(network_stat network_stat2)
for i in 0 1; do
    led="/sys/class/leds/${leds[$i]}"
    [[ -d "$led" ]] || continue
    nic="${nics[$i]:-}"
    if [[ -n "$nic" ]]; then
        echo netdev  > "$led/trigger"
        echo "$nic"  > "$led/device_name"
        echo 1 > "$led/link"; echo 1 > "$led/tx"; echo 1 > "$led/rx"
        echo "255 255 255" > "$led/color" 2>/dev/null
        echo 160 > "$led/brightness"
    else
        echo none > "$led/trigger"
        echo 0    > "$led/brightness"
    fi
done

if [[ -d /sys/class/leds/power ]]; then
    echo none > /sys/class/leds/power/trigger
    echo "255 255 255" > /sys/class/leds/power/color 2>/dev/null
    echo 160 > /sys/class/leds/power/brightness
fi
exit 0
EOF
    chmod 755 /usr/local/bin/ugreen-idx-netled

    cat > /etc/systemd/system/ugreen-idx-netled.service <<'EOF'
[Unit]
Description=UGREEN iDX6011 Pro: LAN/Power LED setup (netdev trigger)
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
    echo "NOTE on bay mapping: MAPPING_METHOD=ata is unverified on the iDX6011 Pro."
    echo "  Verify: generate I/O on one disk and check the right bay blinks."
    echo "  If swapped: run 'ugreen-detect-disks' and switch /etc/ugreen-leds.conf"
    echo "  to MAPPING_METHOD=serial with your serial list."
    echo
    echo "NOTE on the CLI: only use ugreen_leds_cli with the module unloaded"
    echo "  (systemctl stop ugreen-diskiomon ugreen-idx-netled; rmmod led_ugreen)."
else
    log "Kernel module skipped — installing static boot service (CLI variant)"
    cat > /usr/local/bin/ugreen-leds-static <<'EOF'
#!/bin/bash
# Stops the rolling animation at boot and sets a static LED state (CLI variant).
export UGREEN_MODEL=idx6011
/usr/local/bin/ugreen_leds_cli power -on -color 255 255 255 -brightness 144
/usr/local/bin/ugreen_leds_cli netdev netdev2 -on -color 255 255 255 -brightness 96
/usr/local/bin/ugreen_leds_cli disk1 disk2 disk3 disk4 disk5 disk6 -on -color 255 255 255 -brightness 64
EOF
    chmod 755 /usr/local/bin/ugreen-leds-static

    cat > /etc/systemd/system/ugreen-leds-static.service <<'EOF'
[Unit]
Description=UGREEN iDX6011 Pro: static front LEDs (CLI, stops rolling animation)
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
