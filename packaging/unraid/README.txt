ug-paneld for Unraid (iDX6011 Pro front panel display)
=======================================================

Requirements
- Unraid 7.x on a UGREEN iDX6011 Pro (kernel with i915, glibc >= 2.34,
  libdrm.so.2 and libcurl.so.4 — all present on stock Unraid 7).
- If the display stays black and the log says "No connected DRM connector
  found", read "Debugging on newer iDX6011 Pro revisions" in the project
  README: on newer hardware revisions you must boot UGOS once and then
  warm-reboot into Unraid so the EC powers the panel (one-time fix).

Install (as root, e.g. via the web terminal)
    cd /tmp
    tar xzf ug-paneld_*_unraid_amd64.tar.gz
    cd ug-paneld
    sh install.sh

This stores everything under /boot/config/ug-paneld/ and adds a start hook
to /boot/config/go, so the daemon survives reboots (Unraid's root filesystem
is rebuilt from flash on every boot).

Configuration
    /boot/config/ug-paneld/config.json   (synced to /etc/ug-paneld/ at start)
    Optional wallpaper: /boot/config/ug-paneld/wallpaper.png (258x960 PNG)
    Apply changes with: sh /boot/config/ug-paneld/start.sh

Log
    /var/log/ug-paneld.log

Touchscreen on Unraid — KNOWN LIMITATION
    The display and the front LEDs work on stock Unraid. The TOUCHSCREEN does
    not, because Unraid's kernel does not ship the Intel LPSS / DesignWare I2C
    driver (i2c-designware, intel-lpss). The touch panel hangs off that I2C
    controller, so without the driver its bus never appears and nothing in
    userspace can reach the panel. The dashboard simply stays on and is fully
    readable — it just isn't touch-controlled. The log shows:
        Touch: no touchscreen I2C bus found — the kernel is missing the Intel
        LPSS / DesignWare I2C driver
    Confirm with `ls /sys/bus/i2c/devices/`: you will see only an i801 SMBus and
    i915 gmbus/AUX adapters, no DesignWare/LPSS adapter, even though the panel's
    ACPI device (MSFT8000 / CUST0000) is present under /sys/bus/acpi/devices/.

    To make touch work you need a custom Unraid kernel built with
    CONFIG_I2C_DESIGNWARE_PLATFORM + CONFIG_MFD_INTEL_LPSS_PCI/ACPI (e.g. via the
    Unraid Kernel Helper). Once such a kernel exposes i2c-MSFT8000:00 /
    i2c-CUST0000:00, ug-paneld picks the touch up automatically — start.sh
    already loads the modules and frees the controller. If you know the touch
    bus, you can instead set "touch_device": "/dev/i2c-N" in config.json.

Uninstall
    sh uninstall.sh
