ug-fand — fan monitor + control for the UGREEN iDX6011
======================================================

Fan-only daemon (NO display) for UGREEN iDX6011 NAS on a stock Linux
(Proxmox / TrueNAS SCALE / Unraid / Debian), where UGOS' proprietary driver is
absent. Pure userspace, talks to the ITE IT55xx embedded controller directly.
NOT for UGOS (its driver owns the EC).

The model is auto-detected by DMI:
    iDX6011 Pro       -> 4 fans (CPU + system)
    iDX6011 non-Pro   -> 2 system fans (no separate CPU fan); they also cool the
                         CPU, so they follow whichever of cpu_*/sys_* demands more.

WARNING: the bundled fan curves are conservative starting points. Writing fan
registers can overheat the NAS if a curve is wrong — verify on your hardware.

Install
-------
Proxmox / Debian (systemd):
    sudo sh install.sh
    # or install the .deb:  sudo dpkg -i ug-fand_<ver>_amd64.deb

TrueNAS SCALE (read-only/ephemeral root -> pool dataset + Post-Init script):
    sudo sh install.sh /mnt/<your-pool>/ug-fand

Unraid (FAT flash boot -> /boot/config + /boot/config/go hook):
    sudo sh install.sh

On TrueNAS and Unraid the config + binary live on persistent storage (pool /
flash) and are re-applied on every boot, so nothing is lost on reboot or OS
update. On Proxmox the .deb keeps /etc/ug-fand/config across upgrades (conffile).

Configure
---------
    mode=default        # silent | default | turbo
    interval=3          # seconds between updates
    cpu_*/sys_* curves  # TEMP:PERCENT points (see comments in the file)

    Proxmox:  /etc/ug-fand/config        (hot-reloads on save)
    TrueNAS:  /mnt/<pool>/ug-fand/config -> then: sh /mnt/<pool>/ug-fand/start.sh
    Unraid:   /boot/config/ug-fand/config -> then: sh /boot/config/ug-fand/start.sh

Monitoring
----------
    cat /run/ug-fand/status     # RPM, temps, mode, duty
    (model line is logged at start: "model=iDX6011 non-Pro ..." / "... Pro ...")

Web dashboard (optional)
------------------------
A browser dashboard (system stats + fan mode/curve control) is bundled but OFF
by default. Enable it by setting a port in the config, then restart the daemon:

    api_port=8765           # then open http://<nas-ip>:8765/
    #api_password=secret    # optional, protects the fan-control actions only
    #storage_path=/mnt/tank # which mountpoint the Storage widget shows (default /)

Especially handy on the non-Pro (no display). LAN only — do NOT expose it to the
internet. Changing api_port needs a restart; mode/curve edits hot-reload. The
frontend is served from /usr/share/ug-fand/web (Proxmox) or the web/ folder next
to start.sh (TrueNAS/Unraid) — override with UG_FAND_WEB_DIR if needed.

Logs
----
    Proxmox:  journalctl -u ug-fand
    TrueNAS / Unraid:  /var/log/ug-fand.log

Uninstall
---------
    Proxmox:  systemctl disable --now ug-fand; rm /usr/bin/ug-fand /lib/systemd/system/ug-fand.service
              (or: sudo dpkg -r ug-fand)
    TrueNAS:  remove the Post-Init script (System Settings > Advanced) and the pool dir
    Unraid:   remove the "# >>> ug-fand >>>" block from /boot/config/go and the
              /boot/config/ug-fand dir
