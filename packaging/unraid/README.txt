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

Uninstall
    sh uninstall.sh
