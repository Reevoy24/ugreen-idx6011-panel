ug-paneld for TrueNAS SCALE / Community Edition (iDX6011 Pro front panel)
=========================================================================

Requirements
- TrueNAS SCALE 24.10+ / TrueNAS Community Edition (Linux-based) on a UGREEN
  iDX6011 Pro. TrueNAS CORE (FreeBSD) is NOT supported.
- Stock system libraries are enough (glibc >= 2.34, libdrm.so.2, libcurl.so.4).
- If the display stays black and the log says "No connected DRM connector
  found", read "Debugging on newer iDX6011 Pro revisions" in the project
  README: on newer hardware revisions you must boot UGOS once and then
  warm-reboot into TrueNAS so the EC powers the panel (one-time fix).

Why no .deb? TrueNAS has a read-only root filesystem and using apt/dpkg there
is unsupported. This package installs to one of your pools instead and starts
via a TrueNAS Post-Init script — that survives reboots AND system updates.

Install (as root, e.g. via SSH)
    cd /tmp
    tar xzf ug-paneld_*_truenas_amd64.tar.gz
    cd ug-paneld
    sh install.sh /mnt/<your-pool>/ug-paneld

Configuration
    /mnt/<your-pool>/ug-paneld/config.json  (synced to /etc/ug-paneld/ at start)
    Optional wallpaper: wallpaper.png (258x960 PNG) next to the config
    Apply changes with: sh /mnt/<your-pool>/ug-paneld/start.sh

Log
    /var/log/ug-paneld.log

Uninstall
    sh uninstall.sh     (then delete the install directory on your pool)
