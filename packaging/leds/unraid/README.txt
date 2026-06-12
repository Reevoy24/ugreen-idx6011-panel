UGREEN iDX6011 Pro front LEDs for Unraid (static variant)
==========================================================

What this does
--------------
Without UGOS the 9 front status LEDs (power, 2x LAN, 6x disk) cycle a
rolling left-to-right animation forever. This package stops that animation
at every boot, sets a calm base LED state, and runs a small userspace
activity monitor (ugreen-leds-mon.sh): disk and network LEDs switch to the
LED controller's hardware blink mode while there is I/O or traffic, and
back to solid when idle. No kernel module involved — it survives every
Unraid update. It uses the statically linked `ugreen_leds_cli` from
klein0r's fork of ugreen_leds_controller, which added the iDX6011 Pro
protocol:  https://github.com/klein0r/ugreen_leds_controller

Install (as root, from this directory)
--------------------------------------
    sh install.sh

This persists the CLI + start script under /boot/config/ugreen-leds and
hooks into /boot/config/go. It also runs once immediately — the rolling
animation stops right away.

Customize
---------
Edit /boot/config/ugreen-leds/start.sh and re-run it:
    sh /boot/config/ugreen-leds/start.sh

CLI examples (run as root; UGREEN_MODEL=idx6011 must be set):
    export UGREEN_MODEL=idx6011
    ugreen_leds_cli all -status
    ugreen_leds_cli all -off
    ugreen_leds_cli power -on -color 0 0 255 -brightness 128
    ugreen_leds_cli netdev -color 255 0 0 -blink 400 600
LED names: power, netdev, netdev2, disk1..disk6, all

Activity monitor notes
----------------------
Polling is coarse (default every 2 s) — LEDs indicate "busy vs idle", not
per-I/O flicker like the kernel-module setup on Proxmox/Debian. Overrides
go into /boot/config/ugreen-leds/ugreen-leds-mon.conf, e.g.:
    INTERVAL=2
    DISKS="sda sdb sdc sdd sde sdf"   # bay order disk1..disk6
    NICS="eth0 eth1"                  # LAN1 LAN2
The default disk order is /sys/block sd* sorted, which usually matches the
bays — verify by generating I/O on one disk and watching which LED blinks.
Monitor log: /var/log/ugreen-leds-mon.log

Details and the full Proxmox/Debian setup (kernel module, per-I/O
triggers, SMART health colors):
https://github.com/Reevoy24/ugreen-idx6011-pro-nas-display#front-panel-leds

Uninstall
---------
    sh uninstall.sh
