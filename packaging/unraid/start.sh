#!/bin/sh
# ug-paneld Unraid launcher. Called from /boot/config/go at boot (via sh, the
# flash drive is FAT and cannot hold exec bits). Copies the binary off the
# flash drive, syncs the config, and starts the daemon.
PERSIST=/boot/config/ug-paneld
BIN=/usr/local/bin/ug-paneld

mkdir -p /usr/local/bin /etc/ug-paneld
cp -f "$PERSIST/ug-paneld" "$BIN"
chmod 755 "$BIN"
[ -f "$PERSIST/config.json" ] && cp -f "$PERSIST/config.json" /etc/ug-paneld/config.json
# Persist runtime settings (panel/web edits) on the flash drive, not in ephemeral
# /etc, so they survive a reboot. The daemon reads/writes this path directly.
export UG_PANELD_STATE="$PERSIST/state.json"
# Mirror panel/web fan-mode + curve edits to the flash copy too, so they survive
# a reboot (start.sh restores /etc/ug-fand/config from $PERSIST/fand-config below).
[ -f "$PERSIST/ug-fand" ] && export UG_FAND_PERSIST="$PERSIST/fand-config"
[ -f "$PERSIST/wallpaper.png" ] && cp -f "$PERSIST/wallpaper.png" /etc/ug-paneld/wallpaper.png
# serve the built-in wallpapers + web dashboard straight from the flash dir
# (no /usr/share copy needed)
[ -d "$PERSIST/wallpapers" ] && export UG_PANELD_WP_DIR="$PERSIST/wallpapers"
[ -d "$PERSIST/web" ] && export UG_PANELD_WEB_DIR="$PERSIST/web"

# --- touchscreen prerequisites -------------------------------------------
# Unlike the Proxmox .deb (which ships /etc/modprobe.d/no-i2c-hid.conf), Unraid
# has no persistent blacklist and its rootfs is rebuilt from flash every boot,
# so we set everything up here on each start.
#
# 1) Free the touch controller from the kernel HID-over-I2C driver. If it stays
#    bound, it owns I2C address 0x3b and the daemon's ioctl(I2C_SLAVE) fails with
#    EBUSY -> touch is dead while the display still works. Blacklist for future
#    boots AND unload it now in case it already grabbed the controller.
mkdir -p /etc/modprobe.d
echo "blacklist i2c_hid_acpi" > /etc/modprobe.d/ug-paneld-no-i2c-hid.conf
rmmod i2c_hid_acpi 2>/dev/null

# 2) Load the I2C host adapter the touch controller hangs off (Intel LPSS /
#    DesignWare) and the /dev/i2c-N character devices. NOTE: stock Unraid's
#    kernel does NOT ship the LPSS/DesignWare I2C driver, so on a stock kernel
#    these are harmless no-ops and the touchscreen stays unavailable (the
#    display and LEDs still work). Touch on Unraid needs a custom kernel built
#    with I2C_DESIGNWARE_PLATFORM + MFD_INTEL_LPSS_*; these modprobes then make
#    it work. Also no-ops when the drivers are built-in or already loaded.
modprobe intel_lpss_pci 2>/dev/null
modprobe i2c_designware_platform 2>/dev/null
modprobe i2c_designware_core 2>/dev/null
modprobe i2c-dev 2>/dev/null

# 3) The go-hook runs early in boot; give udev a moment and wait (bounded) for
#    the touch controller to enumerate before starting, so the daemon's unbind
#    and bus resolution have a target.
command -v udevadm >/dev/null 2>&1 && udevadm settle 2>/dev/null
i=0
while [ "$i" -lt 15 ]; do
    [ -e /sys/bus/i2c/devices/i2c-CUST0000:00 ] && break
    [ -e /sys/bus/i2c/devices/i2c-MSFT8000:00 ] && break
    i=$((i + 1))
    sleep 1
done
# -------------------------------------------------------------------------

pkill -x ug-paneld 2>/dev/null && sleep 1
nohup "$BIN" >/var/log/ug-paneld.log 2>&1 &
echo "ug-paneld started (log: /var/log/ug-paneld.log)"

# fan control daemon (bundled) — copied off the flash drive like the panel
if [ -f "$PERSIST/ug-fand" ]; then
    cp -f "$PERSIST/ug-fand" /usr/local/bin/ug-fand
    chmod 755 /usr/local/bin/ug-fand
    mkdir -p /etc/ug-fand
    [ -f "$PERSIST/fand-config" ] && cp -f "$PERSIST/fand-config" /etc/ug-fand/config
    modprobe drivetemp 2>/dev/null   # exposes SATA drive temps as hwmon
    pkill -x ug-fand 2>/dev/null && sleep 1
    nohup /usr/local/bin/ug-fand >/var/log/ug-fand.log 2>&1 &
    echo "ug-fand started (log: /var/log/ug-fand.log)"
fi
