UGREEN iDX6011 Pro front LEDs for Unraid (static variant)
==========================================================

What this does
--------------
Without UGOS the 9 front status LEDs (power, 2x LAN, 6x disk) cycle a
rolling left-to-right animation forever. This package stops that animation
at every boot and sets a calm static LED state (power + LANs + disks on,
white, dimmed). It uses the statically linked `ugreen_leds_cli` from
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

Limits
------
This is the static variant: no live disk-activity or network blinking.
That needs the led-ugreen kernel module compiled for the exact Unraid
kernel — ich777's Unraid LED plugin does that for the DX/DXP series but
does not include iDX6011 Pro support yet. Details and the full
Proxmox/Debian setup:
https://github.com/Reevoy24/ugreen-idx6011-pro-nas-display#front-panel-leds

Uninstall
---------
    sh uninstall.sh
