ug-fand — fan monitor + control for the UGREEN iDX6011 Pro
==========================================================

Restores fan monitoring AND control on a stock Linux (Proxmox / TrueNAS /
Debian), where UGOS' proprietary driver is absent. Pure userspace, talks to the
ITE IT55xx embedded controller directly. NOT for UGOS (its driver owns the EC).

WARNING: the bundled fan curves are conservative starting points. Writing fan
registers can overheat the box if a curve is wrong — verify on your hardware.

Install
-------
Proxmox / Debian (systemd):
    sudo sh install.sh

TrueNAS SCALE (read-only root -> installs onto a pool + Post-Init script):
    sudo sh install.sh /mnt/<your-pool>/ug-fand

Configure
---------
    /etc/ug-fand/config
        mode=default        # silent | default | turbo
        interval=3          # seconds between updates

Apply a config change:
    Proxmox:  systemctl restart ug-fand   (or it hot-reloads on file change)
    TrueNAS:  edit /mnt/<pool>/ug-fand/config, then: sh /mnt/<pool>/ug-fand/start.sh

Monitoring
----------
    cat /run/ug-fand/status     # RPM, temps, mode, duty

Logs
----
    Proxmox:  journalctl -u ug-fand
    TrueNAS:  /var/log/ug-fand.log

Uninstall
---------
    Proxmox:  systemctl disable --now ug-fand; rm /usr/bin/ug-fand /lib/systemd/system/ug-fand.service
    TrueNAS:  remove the Post-Init script (System Settings > Advanced) and the pool dir
