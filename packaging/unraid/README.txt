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

Touchscreen not responding (display works)
    start.sh blacklists i2c_hid_acpi and loads the I2C adapter so the daemon can
    drive the touch controller directly. If taps still do nothing, check the log:
      "Could not set I2C address 0x3b ... Device or resource busy"  -> the HID
      driver still owns the controller; reboot so the blacklist takes effect, or
      run start.sh again.
      "no known touchscreen device present" / "guessing /dev/i2c-2"  -> the touch
      I2C adapter did not load; report your `ls /sys/bus/i2c/devices/` output.
      "i2c-hid: ... unbound" or "... already free for direct I2C", followed by
      "Touch: first contact detected ..." on a tap  -> touch is working.
    For a verbose I2C frame dump set "debug": true in config.json and re-run
    start.sh, then share the "Touch raw:" lines.

Uninstall
    sh uninstall.sh
