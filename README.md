# Ugreen Panel Daemon - Display controller for Ugreen's iDX6011 Pro

![ug-paneld running on the Ugreen NAS iDX6011 Pro](./images/ug-paneld-image.JPG)

Ugreen's iDX6011 Pro NAS has a 258x960 portrait display on the front panel. Under UGOS, it shows system stats via a proprietary application (`mini_screen`) that depends on UGOS services and kernel modules.

When running Proxmox, Debian, or other Linux distributions, the display, backlight, and touchscreen don't work out of the box. Ugreen Panel Daemon (ug-paneld) is a replacement dashboard that drives all three directly from userspace, without the proprietary UGOS backend.

Also without UGOS: the 9 front status LEDs are stuck in a rolling demo animation. This repo ships a setup script for those too — see [Front panel LEDs](#front-panel-leds).

## What it does

A swipeable multi-page dashboard in the style of the UGOS mini-screen, with a
working touchscreen:

1. **Home** — date, clock, big CPU ring, glass tiles for RAM, temperature, live network rates and system storage, uptime strip, wallpaper
2. **Hardware** — CPU load with history chart, temperature, memory, Intel GPU load (i915 PMU), uptime
3. **Network** — total ↓/↑ throughput plus per-interface cards with IPv4/IPv6 and link status
4. **Disks** — every SATA/NVMe drive with capacity and temperature
5. **Proxmox** — running VMs/LXCs with status dots (page only appears on PVE hosts)
6. **OPNsense** — WAN throughput gauges, gateway status, updates, DHCP leases, DNS stats (only when configured)

Swipe down from the top edge for the **settings panel** (like UGOS), organized
into grouped list cards: brightness slider, a Display section (screen-off
timeout, wallpaper switcher, language Deutsch/English), an LEDs section with
the front **status LED toggle + night mode** (appears when the
[LED setup](#front-panel-leds) is installed), and restart/shutdown buttons with
confirmation dialogs. Panel settings persist in `/etc/ug-paneld/state.json` —
your `config.json` is never rewritten.

Four built-in wallpapers ship with the package; your own 258x960 PNG at
`/etc/ug-paneld/wallpaper.png` appears as "Custom" in the switcher.

After the idle timeout the screen turns off (pure black frame, backlight off)
and a tap wakes it — the touch controller stays responsive because the daemon
keeps polling it.

## Hardware

The iDX6011 Pro has three hardware interfaces on the front panel. This project controls all of them from userspace without any kernel modules needed:

| Component | Chip | Interface | How we talk to it |
|-----------|------|-----------|-------------------|
| Display | eDP panel (258x960, 32bpp ARGB) | DRM | Standard Linux DRM, device + connector auto-detected |
| Backlight | ITE IT55xx Embedded Controller | x86 port I/O (`0x62`/`0x66`) | `iopl(3)` + `outb`/`inb` |
| Touchscreen | AXS15231B-compatible, ACPI id `CUST0000:00` | I2C, address `0x3b` | bus auto-detected from the ACPI device link |

## Front panel LEDs

The iDX6011 Pro also has **9 RGB status LEDs** (power, 2x LAN, 6x disk) driven by
a separate MCU (Holtek HT32F52231, I2C address `0x3a` on the SMBus). Without
UGOS they cycle a left-to-right rolling animation forever.

The iDX series uses a different LED protocol than the older DX/DXP models, so
the well-known [ugreen_leds_controller](https://github.com/miskcoo/ugreen_leds_controller)
did not work on this device. The protocol was reverse-engineered in
[miskcoo/ugreen_leds_controller#93](https://github.com/miskcoo/ugreen_leds_controller/issues/93)
and implemented in [klein0r's fork](https://github.com/klein0r/ugreen_leds_controller) —
all credit to them. This repo ships a turnkey setup script on top of that fork
([tools/setup-ugreen-leds.sh](tools/setup-ugreen-leds.sh)). Run it as root on
the Proxmox/Debian **host** itself (not inside a VM/LXC):

```bash
wget https://raw.githubusercontent.com/Reevoy24/ugreen-idx6011-pro-nas-display/master/tools/setup-ugreen-leds.sh
bash setup-ugreen-leds.sh
```

The script:

- builds the `ugreen_leds_cli` tool from klein0r's fork and takes over the LED
  MCU — **the rolling animation stops immediately**,
- installs the `led-ugreen` kernel module via DKMS (survives kernel updates),
  exposing the LEDs as `/sys/class/leds/{power,network_stat,network_stat2,disk1..6}`,
- enables services: disk LEDs show activity and SMART health
  (`ugreen-diskiomon`), the two LAN LEDs blink on traffic (`ugreen-idx-netled`),
  power LED solid white — all persistent across reboots,
- falls back to a static LED state (animation stopped, everything lit white) if
  kernel headers are missing.

Notes:

- **Bay order:** the default ata-based disk→LED mapping is not yet verified on
  the iDX6011 Pro. Generate I/O on one disk and check that the right bay
  blinks; if the order is wrong, run `ugreen-detect-disks` and switch
  `/etc/ugreen-leds.conf` to `MAPPING_METHOD=serial`.
- **LAN port order:** if LAN1/LAN2 are swapped, set
  `NETLED_IFACES="<nic1> <nic2>"` in `/etc/default/ugreen-idx-netled`.
- **Manual control:** the CLI tool and the kernel module conflict. To use
  `ugreen_leds_cli` by hand, stop the services and `rmmod led_ugreen` first.

### LED toggle and night mode on the display

When ug-paneld (v1.2.0+) detects the LED setup, the pull-down settings panel
gains two rows:

- **Status LEDs** — turns all front LEDs on/off (off stops `ugreen-diskiomon`
  and zeroes every LED; on restarts the monitors, which restore activity
  blinking and health colors). The choice survives reboots via `state.json`.
- **LED night mode** — turns the LEDs off automatically during a configurable
  night window (default **21:00–08:00**, see `led_night_start` /
  `led_night_end` in `config.json`). Tapping the LEDs back on during the
  window keeps them on until the window ends; the next night they go off
  again. The schedule also runs while the display itself is asleep.

### Front LEDs on TrueNAS / Unraid

The script needs `apt` + DKMS, so it only runs on Proxmox/Debian. On TrueNAS
SCALE and Unraid the situation is:

- `ugreen_leds_cli` is **statically linked** — build it once on any Debian
  machine (or grab it from `/usr/local/bin/` after running the script on
  Proxmox) and copy it over; it runs fine on both platforms. Calling it from a
  TrueNAS Post-Init script or the Unraid `go` file is enough to stop the
  rolling animation and set a static LED state at every boot.
- Live disk/network activity LEDs need the `led-ugreen` kernel module compiled
  for the exact platform kernel. The existing community packages
  ([ich777's Unraid plugin](https://forums.unraid.net/topic/168423-ugreen-nas-led-control/),
  the TrueNAS prebuilt modules) are based on upstream and do **not** include
  iDX6011 Pro support yet.

Open an issue if you want ready-made TrueNAS/Unraid packages for the
static-LED variant — the packaging exists for ug-paneld already and could be
extended.

## Install from release

### Proxmox / Debian / other systemd distros

Download the latest `.deb` from the [releases page](../../releases) and install it:

```bash
dpkg -i ug-paneld_*_amd64.deb
systemctl start ug-paneld
```

This installs the binary, systemd service, and blacklists the `i2c-hid-acpi` module. There is a "no-blacklist" .deb package that can be installed if you would prefer to disable the i2c-hid-acpi module yourself. 

The service is enabled automatically on install.

### TrueNAS SCALE / Community Edition

TrueNAS (Linux-based; CORE/FreeBSD is not supported) has a read-only root
filesystem, so there is a dedicated tarball instead of a .deb. It installs to
one of your pools and starts via a TrueNAS Post-Init script, which survives
reboots and system updates:

```bash
cd /tmp
tar xzf ug-paneld_*_truenas_amd64.tar.gz && cd ug-paneld
sh install.sh /mnt/<your-pool>/ug-paneld
```

Config lives next to the binary (`/mnt/<your-pool>/ug-paneld/config.json`) and
is synced to `/etc/ug-paneld/` at each start. See the `README.txt` inside the
tarball for details.

### Unraid

Unraid rebuilds its root filesystem from flash on every boot, so the Unraid
tarball persists everything under `/boot/config/ug-paneld/` and hooks into
`/boot/config/go`:

```bash
cd /tmp
tar xzf ug-paneld_*_unraid_amd64.tar.gz && cd ug-paneld
sh install.sh
```

Config lives at `/boot/config/ug-paneld/config.json`; apply changes with
`sh /boot/config/ug-paneld/start.sh`. See the `README.txt` inside the tarball.

> [!NOTE]
> The TrueNAS and Unraid packages are new and not yet field-tested on those
> platforms — the binary is identical to the Proxmox/Debian one, only the
> packaging differs. Feedback welcome. The display/panel behaviour (including
> the newer-revision EC quirk described below) is identical on all distros.

## Build from source

### Dependencies

```bash
apt install build-essential libdrm-dev libcurl4-openssl-dev pkg-config

# Optional, for touchscreen debugging
apt install i2c-tools
```

### Build

```bash
make
```

### Install

```bash
make install   # copies to /usr/bin/ug-paneld
```

To build `.deb` packages locally (same layout as the release workflow):

```bash
./build-deb.sh 1.0.2
```

To build the TrueNAS SCALE and Unraid tarballs:

```bash
./build-tarballs.sh 1.0.2
```

### Run

The application requires root access (for EC port I/O and I2C access).

> [!IMPORTANT]
> The `i2c-hid-acpi` kernel module will grab the touchscreen's I2C device on boot and block direct access. ug-paneld unbinds it automatically at startup (it knows both the `CUST0000:00` and `MSFT8000:00` ids used by different hardware revisions, and logs what it did). To do it manually instead:

```bash
# Option A: unbind for this session (the id depends on your hardware revision)
echo "i2c-CUST0000:00" > /sys/bus/i2c/drivers/i2c_hid_acpi/unbind 2>/dev/null
echo "i2c-MSFT8000:00" > /sys/bus/i2c/drivers/i2c_hid_acpi/unbind 2>/dev/null

# Option B: blacklist permanently
echo "blacklist i2c_hid_acpi" > /etc/modprobe.d/no-i2c-hid.conf
```

Then run:

```bash
ug-paneld
```

Press `Ctrl+C` twice to exit. The backlight turns off on shutdown.

### Start at boot

If you built from source, copy the included service file:

```bash
cp ug-paneld.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now ug-paneld
```

## Configuration

All settings are optional. Create `/etc/ug-paneld/config.json`:

```json
{
    "poll_rate": 2,
    "drm_device": "",
    "connector": "auto",
    "drm_probe_timeout": 10,
    "i2c_device": "auto",
    "touch_device": "auto",
    "debug": false,
    "brightness": 100,
    "backlight_timeout": 30,
    "sleep_brightness": 0,
    "led_night_start": "21:00",
    "led_night_end": "08:00",
    "opnsense_url": "https://192.168.1.1:8443",
    "opnsense_key": "your-api-key",
    "opnsense_secret": "your-api-secret",
    "wan_interface": "wan",
    "wan_max_mbps": 1000
}
```

Brightness, screen-off timeout, wallpaper, and language set via the on-screen
settings panel are stored separately in `/etc/ug-paneld/state.json` and
override the config defaults.

| Key | Default | Description |
|-----|---------|-------------|
| `poll_rate` | `2` | How often to poll system stats (seconds) |
| `drm_device` | auto-detect | DRM device path, e.g. `/dev/dri/card0`. Empty = scan `/dev/dri/card*`. (Legacy key `drm_card` still works.) |
| `connector` | `auto` | DRM connector to drive: a name like `eDP-1`/`DSI-1`, a numeric connector id, or `auto` (first connected connector with modes) |
| `drm_probe_timeout` | `10` | Seconds to wait at startup for a connected DRM connector before giving up |
| `i2c_device` | `auto` | ACPI id to unbind from the `i2c_hid_acpi` driver so the touchscreen is accessible: `auto` (tries the known ids `CUST0000:00` and `MSFT8000:00`), `none` (skip), or a specific id like `MSFT8000:00` |
| `debug` | `false` | Verbose DRM probe logging |
| `touch_device` | `auto` | Touchscreen I2C bus: `auto` resolves it from the ACPI device link; explicit paths like `/dev/i2c-2` still work |
| `brightness` | `100` | Backlight brightness (1-100) |
| `backlight_timeout` | `30` | Seconds before the screen sleeps (0 to disable) |
| `sleep_brightness` | `0` | Backlight % while asleep. `0` = fully off (a black frame is shown and the touch chip stays poll-awake, so a tap still wakes the screen) |
| `led_night_start` | `21:00` | Start of the front-LED night window (`HH:MM`); used when LED night mode is enabled in the settings panel |
| `led_night_end` | `08:00` | End of the front-LED night window (`HH:MM`) |
| `opnsense_url` | | OPNsense base URL (leave empty to disable) |
| `opnsense_key` | | OPNsense API key |
| `opnsense_secret` | | OPNsense API secret |
| `wan_interface` | `wan` | Interface name for throughput monitoring |
| `wan_max_mbps` | `1000` | Max WAN speed in Mbps (scales the arc gauge) |
| `api_port` | `0` | HTTP API port for backlight control (0 to disable) |

## Control remotely

Set `api_port` in your config (e.g. `9101`) to enable an HTTP API for backlight control. You can use this with Home Assistant to get a light entity with a brightness slider.

**API:**

Brightness set through the API is kept through sleep/wake cycles.

```bash
# Get current state
curl http://192.168.1.110:9101/backlight
# {"state":"on","brightness":100}

# Turn off
curl -X POST http://192.168.1.110:9101/backlight -d '{"state":"off"}'

# Turn on
curl -X POST http://192.168.1.110:9101/backlight -d '{"state":"on"}'

# Set brightness (0-100)
curl -X POST http://192.168.1.110:9101/backlight -d '{"brightness":50}'
```

**Home Assistant `configuration.yaml`:**
```yaml
rest_command:
  nas_display_backlight:
    url: "http://192.168.1.110:9101/backlight"
    method: POST
    content_type: "application/json"
    payload: "{{ payload }}"

rest:
  - resource: "http://192.168.1.110:9101/backlight"
    scan_interval: 10
    sensor:
      - name: "NAS Display State"
        value_template: "{{ value_json.state }}"
      - name: "NAS Display Brightness"
        value_template: "{{ value_json.brightness }}"

template:
  - light:
      - name: "NAS Display"
        unique_id: nas_display_backlight
        optimistic: true
        state: "{{ 'on' if states('sensor.nas_display_state') == 'on' else 'off' }}"
        level: "{{ (states('sensor.nas_display_brightness') | int * 2.55) | round }}"
        turn_on:
          service: rest_command.nas_display_backlight
          data:
            payload: '{"state":"on"}'
        turn_off:
          service: rest_command.nas_display_backlight
          data:
            payload: '{"state":"off"}'
        set_level:
          service: rest_command.nas_display_backlight
          data:
            payload: '{"brightness":{{ (brightness / 2.55) | round }}}'
```

This gives you a standard light entity with a brightness slider in Home Assistant.

## How it works

### Backlight (EC Port I/O)

The ITE IT55xx Embedded Controller controls the backlight through x86 port I/O. The same EC handles fan control, watchdog, and power management in the proprietary `ug_idx6011pro-sio.ko` module. We only use the backlight portion.

**Protocol:**
1. Wait for Input Buffer Full (IBF) flag to clear on port `0x66`
2. Write `0x81` to port `0x66` (EC write-memory command)
3. Wait for IBF clear, write `0xA5` to port `0x62` (backlight address)
4. Wait for IBF clear, write brightness value to port `0x62`

Brightness is inverted: `1` = full brightness, `198` = off.

You can test this from the command line with Python:

```bash
# Turn off the backlight
python3 -c "
f=open('/dev/port','r+b',buffering=0)
import time
def wb(p,v): f.seek(p); f.write(bytes([v]))
def rb(p): f.seek(p); return f.read(1)[0]
def wait():
    for _ in range(5000):
        if not (rb(0x66) & 0x02): return
        time.sleep(0.0001)
wait(); wb(0x66,0x81); wait(); wb(0x62,0xA5); wait(); wb(0x62,198)
"
```

### Touchscreen (I2C)

The Awinic AXS15231B touch controller sits on I2C bus 2 at address `0x3b`. The same controller is used on some ESP32 boards, and the protocol matches [ESPHome's AXS15231B implementation](https://api-docs.esphome.io/axs15231__touchscreen_8cpp_source).

**Read command (8 bytes):**
```
0xB5 0xAB 0xA5 0x5A 0x00 0x00 0x00 0x08
```

**Response (8 bytes):**
```
Byte:  [0]       [1]           [2]          [3]     [4]         [5]     [6]  [7]
Field: gesture   num_touches   event|x_hi   x_lo    id|y_hi     y_lo    ??   ??
```

Some bytes pack two values by splitting the bits. For example, byte 2 stores both the event type and part of the X coordinate. The top 2 bits are the event type, the bottom 4 bits are the high nibble of the X coordinate. Byte 3 has the remaining 8 bits of X. Same for byte 4. The top 4 bits are the touch point ID, bottom 4 are the high nibble of Y.

**Decoding:**
```c
uint8_t event = byte[2] >> 6;              // 0=down, 1=up, 2=contact
uint16_t x = (byte[2] & 0x0F) << 8 | byte[3];  // 12-bit, 0-257
uint16_t y = (byte[4] & 0x0F) << 8 | byte[5];  // 12-bit, 0-959
uint8_t id = byte[4] >> 4;                 // touch point ID (multi-touch)
```

**Testing from the command line:**
```bash
# Make sure i2c-hid-acpi is unbound first
i2ctransfer -y 2 w8@0x3b 0xB5 0xAB 0xA5 0x5A 0x00 0x00 0x00 0x0E r14
```

Example responses:
```
Not touching:     0x00 0x01 0x40 0x56 0x01 0x97 ...
Touching center:  0x00 0x01 0x80 0x41 0x01 0x54 ...
Touching top-left: 0x00 0x01 0x80 0x1f 0x00 0x1e ...
```

### Troubleshooting: touch

The touchscreen bus is resolved automatically from the ACPI device link
(`/sys/bus/i2c/devices/i2c-CUST0000:00` → parent `i2c-N`), so bus-number
shifts between revisions/boots are handled. If touch still misbehaves, check
`journalctl -u ug-paneld` for the `Touch:` lines (resolved bus, first contact,
I2C failures).

> [!WARNING]
> Do not diagnose the touch controller with ug-paneld stopped: the chip
> auto-sleeps when nobody polls it and then answers every I2C transaction
> (including HID descriptor reads) with constant `0x23` bytes — which looks
> exactly like a broken chip. While the daemon runs, its 33–50 ms polling
> keeps the controller awake; that is also why wake-by-tap works even with
> the backlight fully off.

## Debugging on newer iDX6011 Pro revisions

Newer revisions of the iDX6011 Pro differ from the original in two ways:

1. **The touchscreen enumerates as `MSFT8000:00` instead of `CUST0000:00`.** ug-paneld handles both automatically; if your unit uses yet another id, set it via `i2c_device` in the config.
2. **Some unit/kernel combinations fail to bring up the internal panel at all.** `dmesg` then shows lines like `[drm] [ENCODER:...:DDI A/PHY A] failed to retrieve link info, disabling eDP` and `[drm] Cannot find any crtc or sizes`, and every connector under `/sys/class/drm/` stays `disconnected`.

Useful commands when the display stays black:

```bash
# Which DRM connectors does the kernel expose?
ls /sys/class/drm/

# Connection status of each connector — exactly one should say "connected"
for x in /sys/class/drm/card0-*/status; do echo "$x"; cat "$x"; done

# Which I2C devices exist? (shows whether your touchscreen is CUST0000 or MSFT8000)
ls /sys/bus/i2c/devices/

# What did ug-paneld detect and decide?
journalctl -u ug-paneld -n 100 --no-pager
```

At startup ug-paneld probes every DRM device and logs each connector with its
name, id, status, and modes, then waits up to `drm_probe_timeout` seconds for a
connected one to appear. If none does, it logs:

```
No connected DRM connector found; kernel did not expose internal panel.
```

and exits with code 2. The systemd service treats exit code 2 as unrecoverable
(`RestartPreventExitStatus=2`), so it fails cleanly instead of restart-looping.

If you hit that message, the panel cannot be driven from userspace on that
boot: the kernel's display driver never registered a usable connector.

### Why this happens — and the fix (verified on a newer revision)

On newer iDX6011 Pro revisions the panel's power rail is switched by the
ITE Embedded Controller (the same EC that drives the backlight), not by the
Intel PCH. The BIOS declares the panel correctly (eDP on DDI-A/AUX-A, 1 lane,
258x960 — the VBT is healthy), but with the panel unpowered it never answers
on the AUX channel, so i915 logs `failed to retrieve link info, disabling eDP`
and gives up. UGOS powers the panel through its proprietary EC driver
(`ug_idx6011pro_sio.ko`); vanilla Linux doesn't know it has to.

**The practical fix:** boot UGOS once, then *reboot* (do not power off) and
boot your Linux/Proxmox drive via the firmware boot menu (usually F11/F12).
The EC keeps the panel powered, i915 then registers `eDP-1` as `connected`,
and ug-paneld picks it up automatically.

The EC stores this state persistently: on the unit this was verified on, the
panel kept working across reboots, shutdowns, and even a multi-minute mains
power cut. The UGOS warm-boot trick is therefore a one-time fix. Should the
panel ever come up dark again (e.g. after an EC reset or firmware update),
just repeat it.

Note that the DRM card number can change between boots (`card0` vs `card1`);
ug-paneld scans all of them, so this is only relevant when reading
`/sys/class/drm/` yourself.

If a connector **is** connected but the display still stays black, set
`"debug": true` in `/etc/ug-paneld/config.json`, restart the service, and check
the journal for which device/connector/mode was handed to LVGL and where setup
failed.

## OPNsense Integration

If `opnsense_url` is set, the dashboard polls these endpoints:

| Endpoint | What it shows |
|----------|---------------|
| `/api/routes/gateway/status` | Gateway RTT and status |
| `/api/core/firmware/status` | Firmware update availability |
| `/api/dhcpv4/leases/searchLease` | Active DHCP lease count |
| `/api/unbound/overview/totals/0` | DNS queries and blocked percentage |
| `/api/interfaces/traffic/top/{interface}` | WAN in/out throughput |

You'll need to create an API key in OPNsense under System > Access > Users.

## Acknowledgement

[ESPHome AXS15231B driver](https://api-docs.esphome.io/axs15231__touchscreen_8cpp_source) for the touchscreen protocol reference.
