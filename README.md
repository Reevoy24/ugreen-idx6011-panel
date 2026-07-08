# ugreen-idx6011-panel

Touch dashboard, front-LED control and fan control for the UGREEN NASync iDX6011 Pro, plus standalone fan control for the non-Pro iDX6011. Runs on Proxmox, Debian, TrueNAS SCALE and Unraid.

*Community project. Not affiliated with or endorsed by UGREEN.*

[![Release](https://img.shields.io/badge/release-v1.7.4-2ea44f)](../../releases/latest)
![Platforms](https://img.shields.io/badge/runs%20on-Proxmox%20·%20Debian%20·%20TrueNAS%20·%20Unraid-6f42c1)
![Field-tested](https://img.shields.io/badge/field--tested%20on-Proxmox%20VE-success)
![UI](https://img.shields.io/badge/UI-LVGL%209-ff6d00)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

The iDX6011 Pro has a 258×960 touch display on the front. Under UGOS it shows system stats. Under Proxmox, Debian, TrueNAS or Unraid the screen stays black and the 9 front status LEDs run a rolling demo animation forever.

**ug-paneld** fixes both. It draws a UGOS-style touch dashboard entirely from userspace and bundles [fan control](#fan-control). A separate script adds full [front-LED control](#front-panel-leds).

![All dashboard pages](images/pages-overview.png)

**Jump to:** [Features](#features) · [Platform support](#platform-support) · [Install](#install) · [Front panel LEDs](#front-panel-leds) · [Fan control](#fan-control) · [Web dashboard](#web-dashboard) · [Configuration](#configuration) · [Troubleshooting](#troubleshooting)

## Features

- 🖥️ **Swipeable dashboard.** Home (clock, CPU ring, glass tiles), Hardware, Network, Disks, Proxmox guests (PVE hosts only) and an optional OPNsense page.
- 👆 **Working touch.** Swipe between pages and pull down a settings panel for brightness, screen-off timeout, wallpaper, language (EN, DE, ES, FR, PT, ID) and restart/shutdown.
- 🌀 **Fan monitoring and control.** Swipe left from Home for live RPM and temperatures, three modes (Silent, Default, Turbo) and the live curve. The bundled `ug-fand` daemon drives the ITE EC from userspace with no kernel module. It also ships as a display-free build that auto-detects the non-Pro iDX6011.
- 🌐 **Web dashboard.** Opt-in (`api_port`): the whole panel in your browser, including the fan curve editor, every setting, 12h/24h clock, timezone and wallpaper upload.
- 💡 **Front LED control.** Stops the rolling animation and shows disk activity, SMART health and network traffic. On/off plus a night mode (default 21:00 to 08:00) live right on the display.
- 🌙 **Screen sleep.** Backlight off after idle, tap to wake.
- 🎨 **Wallpapers** behind every page (3 built-ins or your own PNG).
- 🏠 **Home Assistant.** The optional HTTP API exposes the backlight as a light entity.
- 📦 **Packaged** for Proxmox/Debian (`.deb`), TrueNAS SCALE and Unraid (tarballs), plus a separate display-free fan-only build for non-Pro units.

## Platform support

| Platform | Display + touch | Fan control | Front LEDs |
|----------|:---------------:|:-----------:|:----------:|
| **Proxmox / Debian** | ✅ field-tested | ✅ field-tested | ✅ field-tested |
| **TrueNAS SCALE** | ✅ confirmed | ✅ verified | ⚠️ not yet confirmed |
| **Unraid** | display ✅ / touch ❌ | ✅ | ❌ |

- **Proxmox / Debian** is the reference platform. Everything is tested on real hardware (a newer-revision iDX6011 Pro).
- **TrueNAS SCALE** ships the identical binaries. The display, touch and fan control are confirmed on real TrueNAS hardware (a user reported smooth swiping, full touch coverage, idle sleep and working web stats; fan install, reboot persistence and mode switching all work). Only the front LEDs are not yet confirmed there, so feedback through the [issues](../../issues) is welcome.
- **Unraid** runs the display fine, since it goes over i915/DRM rather than I2C, but touch and the front LEDs do not work on the stock kernel. Unraid omits the Intel SoC I2C stack this Meteor Lake board needs (`intel_lpss` for the touch bus, and the pinctrl/SMBus pieces for the LED bus), so neither I2C bus comes up. The fix is a custom Unraid kernel with `CONFIG_PINCTRL_METEORLAKE` and `CONFIG_MFD_INTEL_LPSS_*`, not a different LED driver. See [`packaging/unraid/README.txt`](packaging/unraid/README.txt) for the custom-kernel notes.

## Install

Installing **ug-paneld** gives you the touch dashboard **and** fan monitoring and control in one step, because fan control is bundled. The front LEDs are a separate, optional setup (see [Front panel LEDs](#front-panel-leds)). The commands below install **v1.7.4**. If a newer release exists, swap that version into the URL and filename (see the [releases page](../../releases)).

> [!NOTE]
> **Which build do I need?**
> * **iDX6011 Pro** (it has the front display): use the **panel** packages below.
> * **non-Pro iDX6011** (it has no display): skip the panel and install the display-free [**ug-fand v1.1.0**](https://github.com/Reevoy24/ugreen-idx6011-panel/releases/tag/ug-fand-v1.1.0) build instead (`.deb` for Proxmox, tarballs for TrueNAS/Unraid). See [Fan control](#fan-control).

### Proxmox / Debian

```bash
wget https://github.com/Reevoy24/ugreen-idx6011-panel/releases/download/v1.7.4/ug-paneld_1.7.4_amd64.deb
dpkg -i ug-paneld_1.7.4_amd64.deb
```

This installs the binary and enables and starts the service. A no-blacklist variant (`ug-paneld_1.7.4_no-blacklist_amd64.deb`) is available if you would rather manage the `i2c-hid-acpi` module yourself.

### TrueNAS SCALE

The package installs onto one of your pools and registers a Post-Init script, so it starts on every boot without touching the read-only system area.

```bash
wget https://github.com/Reevoy24/ugreen-idx6011-panel/releases/download/v1.7.4/ug-paneld_1.7.4_truenas_amd64.tar.gz
tar xzf ug-paneld_1.7.4_truenas_amd64.tar.gz && cd ug-paneld
sh install.sh /mnt/<your-pool>/ug-paneld
```

### Unraid

Everything persists on the flash drive and hooks into `/boot/config/go`.

```bash
wget https://github.com/Reevoy24/ugreen-idx6011-panel/releases/download/v1.7.4/ug-paneld_1.7.4_unraid_amd64.tar.gz
tar xzf ug-paneld_1.7.4_unraid_amd64.tar.gz && cd ug-paneld
sh install.sh
```

After installing, the dashboard appears on the display and the Fan control page works right away. To run the daemon as a different user or build it yourself, see [Build from source](#build-from-source). Set up the [front LEDs](#front-panel-leds) next if you want them.

> [!IMPORTANT]
> **Display stays black on a Pro unit?** On newer iDX6011 Pro revisions the internal eDP panel does not answer the graphics driver until UGOS has initialized it once, so on a cold Linux boot i915 drops the panel and the screen stays black. One-time fix: boot UGOS, let the front display light up, then reboot into your Linux drive (firmware boot menu, Ctrl+F12). That initialization is non-volatile, so it holds across later reboots and full power-offs; you only do it once per unit. Full background in [Troubleshooting](#troubleshooting).

## Front panel LEDs

The 9 RGB status LEDs (power, 2× LAN, 6× disk) are driven by a separate MCU whose protocol differs from older UGREEN models. It was reverse-engineered in [miskcoo/ugreen_leds_controller#93](https://github.com/miskcoo/ugreen_leds_controller/issues/93) and implemented in [klein0r's fork](https://github.com/klein0r/ugreen_leds_controller) (all credit to them).

### Proxmox / Debian

Full setup: a DKMS kernel module with live activity and SMART health colors. Run as root on the **host**:

```bash
wget https://raw.githubusercontent.com/Reevoy24/ugreen-idx6011-panel/master/tools/setup-ugreen-leds.sh
bash setup-ugreen-leds.sh
```

The rolling animation stops immediately, disk LEDs show activity and health, and the LAN LEDs blink on traffic. The script also installs the kernel-header meta package (`proxmox-default-headers`), so DKMS rebuilds the module on every kernel update and the LEDs keep working across reboots and upgrades. If the LEDs ever drop to a plain static state after a kernel update, the matching headers were missing: just re-run the script to repair it.

Once the setup is installed, the ug-paneld settings panel gains a **Status LEDs** on/off row and a **night mode** row, which turns the LEDs off automatically between `led_night_start` and `led_night_end` (default 21:00 to 08:00). Turning them on during the window overrides it until the window ends.

### TrueNAS SCALE

The LED tarball installs onto one of your pools and registers a Post-Init script, so the LEDs come up on every boot without touching the read-only system area:

```bash
wget https://github.com/Reevoy24/ugreen-idx6011-panel/releases/download/leds-v1.1.1/ugreen-leds_1.1.1_truenas_amd64.tar.gz
tar xzf ugreen-leds_1.1.1_truenas_amd64.tar.gz && cd ugreen-leds
sh install.sh /mnt/<your-pool>/ugreen-leds
```

It stops the animation at boot, sets a calm base state and runs a small userspace activity monitor (busy means a hardware blink, idle means solid). This needs no kernel module and survives every platform update. Per-I/O triggers with SMART colors need the kernel module built for that kernel, which is tracked upstream in [0x556c79/install_ugreen_leds_controller#23](https://github.com/0x556c79/install_ugreen_leds_controller/issues/23). The LED side is not yet confirmed on TrueNAS hardware, so feedback is welcome.

### Unraid

Reported **not working** on the iDX6011. The front-LED MCU lives on the i801 SMBus, and the stock Unraid kernel does not bring that bus up on this Meteor Lake board (the same missing Intel I2C/pinctrl stack that disables touch, see [Platform support](#platform-support)). Neither the kernel driver nor the userspace tool can reach the MCU. The fix is a custom kernel, not a different LED driver. The upstream plugin issue [ich777/unraid-ugreenleds-driver#8](https://github.com/ich777/unraid-ugreenleds-driver/issues/8) is closed, so a plugin fork will not help here.

<details>
<summary><b>LED notes: bay order, LAN order, manual control</b></summary>

* **Bay order:** the default ata-based disk-to-LED mapping is not yet verified on the iDX6011 Pro. Generate I/O on one disk and check that the right bay blinks. If the order is wrong, run `ugreen-detect-disks` and switch `/etc/ugreen-leds.conf` to `MAPPING_METHOD=serial`.
* **LAN port order:** if LAN1 and LAN2 are swapped, set `NETLED_IFACES="<nic1> <nic2>"` in `/etc/default/ugreen-idx-netled` (Proxmox) or `NICS="..."` in `ugreen-leds-mon.conf` (TrueNAS/Unraid).
* **Manual control:** the CLI tool and the kernel module conflict. Stop the LED services and `rmmod led_ugreen` before using `ugreen_leds_cli` by hand.
* **LED toggle semantics:** "off" on the display stops `ugreen-diskiomon` and zeroes every LED, "on" restarts the monitors. The choice persists in `state.json`.

</details>

## Fan control

The iDX6011's fans hang off an **ITE IT55xx embedded controller** reachable as a standard ACPI EC (ports `0x62` and `0x66`). UGOS drives them through a proprietary kernel module, so a stock Linux (Proxmox, TrueNAS, Debian, Unraid) sees no fan sensors or control at all. **`ug-fand`** restores both monitoring and control entirely from userspace, with no kernel module.

It auto-detects the model. The **iDX6011 Pro** has 4 fans (CPU plus system). The **non-Pro iDX6011** has 2 system fans and no separate CPU fan (the system fans cool the CPU too), at different EC offsets.

> [!WARNING]
> The bundled curves are conservative starting points. Writing fan registers can overheat the NAS if a curve is wrong, so verify the result on your hardware.

> [!NOTE]
> **Non-Pro iDX6011?** The panel packages in [Install](#install) are for the Pro and drive the display. The non-Pro iDX6011 has no panel, so install the display-free [**ug-fand v1.1.0**](https://github.com/Reevoy24/ugreen-idx6011-panel/releases/tag/ug-fand-v1.1.0) build instead (a `.deb` for Proxmox, tarballs for TrueNAS/Unraid, config persists across reboots and OS updates). Everything below applies to it too.

### Setup

**With the panel (iDX6011 Pro):** there is nothing to do. `ug-fand` is bundled with the panel package and is already running (the [install](#install) started it). Confirm it:

```bash
systemctl status ug-fand          # Proxmox / Debian
cat /var/log/ug-fand.log          # TrueNAS / Unraid
```

**Standalone (non-Pro, no panel):** download the display-free [ug-fand v1.1.0](https://github.com/Reevoy24/ugreen-idx6011-panel/releases/tag/ug-fand-v1.1.0) build and install it. The commands install v1.1.0; swap in a newer version if one exists.

Proxmox / Debian:

```bash
wget https://github.com/Reevoy24/ugreen-idx6011-panel/releases/download/ug-fand-v1.1.0/ug-fand_1.1.0_amd64.deb
dpkg -i ug-fand_1.1.0_amd64.deb
```

TrueNAS SCALE:

```bash
wget https://github.com/Reevoy24/ugreen-idx6011-panel/releases/download/ug-fand-v1.1.0/ug-fand_1.1.0_truenas_amd64.tar.gz
tar xzf ug-fand_1.1.0_truenas_amd64.tar.gz && cd ug-fand
sh install.sh /mnt/<your-pool>/ug-fand
```

Unraid:

```bash
wget https://github.com/Reevoy24/ugreen-idx6011-panel/releases/download/ug-fand-v1.1.0/ug-fand_1.1.0_unraid_amd64.tar.gz
tar xzf ug-fand_1.1.0_unraid_amd64.tar.gz && cd ug-fand
sh install.sh
```

The standalone build also bundles an **opt-in web dashboard** (system stats plus fan mode and curve control in a browser), handy on the non-Pro that has no display. Enable it by setting `api_port` (and optionally `api_password`) in the config, then open `http://<nas-ip>:<port>/`. LAN only, do not expose it to the internet.

<details>
<summary><b>Building the daemon from source</b></summary>

Build just the daemon (`make fand` produces `ug-fand` at the repo root), then:

```bash
# Proxmox / Debian (systemd) or Unraid (/boot/config + go hook), auto-detected
sudo sh packaging/fand/install.sh

# TrueNAS SCALE (installs onto a pool and registers a Post-Init script)
sudo sh packaging/fand/install.sh /mnt/<pool>/ug-fand
```

</details>

### Configuring the curves

The config lives in `/etc/ug-fand/config`:

```
mode=default       # silent | default | turbo
interval=3         # seconds between updates
# optional per-mode curves, comma-separated temp:speed points (°C : 0-100%):
cpu_default=0:15,60:15,70:38,78:71,86:100
sys_default=0:28,52:28,58:55,63:80,68:100
```

The three modes are temperature-to-speed curves, from silent (quietest) to turbo (coolest). The `cpu_*` curves follow the CPU temperature, the `sys_*` curves follow the disk/NVMe temperature. The reading is smoothed and a speed deadband is applied, so the fans hold a steady speed instead of hunting on brief CPU spikes. A thermal failsafe forces full speed above the critical thresholds (88 °C CPU, 68 °C disks), and a missing temperature reading is treated as "full", so a broken sensor never silences the fans.

Each curve is a comma-separated list of `temp:speed` points: temperature in °C, fan speed in percent (`0` to `100`, where `100` is full). Edit them with any editor:

```bash
sudo nano /etc/ug-fand/config      # or: vi /etc/ug-fand/config
```

Change the numbers and **save**. `ug-fand` reloads the file automatically, no restart needed. Lower the speed numbers (or push the ramp temperatures higher) for a quieter NAS, raise them for a cooler one. For example, to keep the system fans at their quiet floor until the disks get warmer, widen the flat part of the curve:

```
sys_default=0:28,56:28,60:55,64:80,68:100   # quiet up to 56 °C, then ramp
```

Delete a curve line to fall back to the built-in default.

> [!NOTE]
> On TrueNAS and Unraid the system area is rebuilt every boot, so `/etc/ug-fand/config` is re-synced at boot from the copy on your pool or flash drive. With the **bundled install**, a mode or curve change made on the panel or web UI is mirrored back to that copy (`<install-dir>/fand-config`), so it survives a reboot. For the **standalone fan-only daemon** (no panel), edit the pool copy directly (`/mnt/<pool>/ug-fand/config`) and re-run its `start.sh`.

### Monitoring

`ug-fand` writes live values to `/run/ug-fand/status`:

```
$ cat /run/ug-fand/status
mode=default
cpu_temp=44
sys_temp=45
cpufan1=575
cpufan2=599
sysfan1=789
sysfan2=796
cpu_pct=12
sys_pct=28
```

<details>
<summary><b>EC fan registers (drive the fans yourself)</b></summary>

Same EC as the backlight. Read a byte with command `0x80`, write a byte with command `0x81`, address on `0x62`. Wait for IBF (`0x66` bit `0x02`) to clear before each write, and for OBF (bit `0x01`) to be set before a read.

**Tachometer, read as 16-bit big-endian (RPM):**

| Fan | hi / lo |
|-----|---------|
| cpufan1 | `0x34` / `0x35` |
| cpufan2 | `0x36` / `0x37` |
| sysfan1 | `0x38` / `0x39` |
| sysfan2 | `0x3A` / `0x3B` |

**Duty, write per fan (enable byte = `1`, then duty `0..198`):**

| Fan | enable / duty |
|-----|---------------|
| cpufan1 | `0xB0` / `0xB1` |
| cpufan2 | `0xB2` / `0xB3` |
| sysfan1 | `0xB4` / `0xB5` |
| sysfan2 | `0xB6` / `0xB7` |

The table above is the **iDX6011 Pro**. The **non-Pro iDX6011** uses the same EC and protocol but only 2 system fans, at different offsets (duty is still `0..198`):

| Fan | tach hi / lo | enable / duty |
|-----|--------------|---------------|
| sysfan1 | `0x96` / `0x97` | `0x9C` / `0x9D` |
| sysfan2 | `0x98` / `0x99` | `0x9E` / `0x9F` |

Read all four Pro RPMs from the shell:

```bash
python3 - <<'PY'
f=open('/dev/port','r+b',buffering=0)
def rb(p): f.seek(p); return f.read(1)[0]
def wb(p,v): f.seek(p); f.write(bytes([v]))
def ibf():
    for _ in range(20000):
        if not (rb(0x66)&0x02): return
def obf():
    for _ in range(20000):
        if rb(0x66)&0x01: return
def ec(a): ibf(); wb(0x66,0x80); ibf(); wb(0x62,a); obf(); return rb(0x62)
for n,h in (('cpufan1',0x34),('cpufan2',0x36),('sysfan1',0x38),('sysfan2',0x3a)):
    print(n,(ec(h)<<8)|ec(h+1))
PY
```

</details>

## Web dashboard

This is opt-in. Add `api_port` to `config.json` and ug-paneld serves a browser dashboard on the LAN that mirrors the whole panel: live stats (CPU, RAM, temps, uptime, network, disks, Proxmox, OPNsense, GPU, fans), the Silent/Default/Turbo switch and fan curve, and every setting (brightness, screen-off timeout, language, LEDs plus night window, wallpaper with custom upload) plus restart/shutdown. It is built into ug-paneld, so there is no extra service or container.

```json
{
    "api_port": 8080,
    "api_password": "choose-a-password"
}
```

Add these two keys to `/etc/ug-paneld/config.json` (on TrueNAS and Unraid, edit the copy on your pool or flash drive instead, see [Configuration](#configuration)), restart ug-paneld, then open `http://<nas-ip>:8080`. The running ug-paneld version is shown in the dashboard footer, which is handy when filing an issue.

* **Monitoring is open** on the LAN. **Changing settings or fan mode** needs the password when `api_password` is set. **Restart and shutdown always need it** and are refused entirely when no password is set.
* The legacy `GET/POST /backlight` endpoint still works (Home Assistant, see below).

> [!WARNING]
> This is a control surface on a daemon running as root, over plain HTTP. **Use it on the LAN only and never port-forward it to the internet.** Set `api_password` and keep it on a trusted network. (ug-fand's thermal failsafe still forces full speed above the critical thresholds, so a bad curve cannot overheat the NAS.)

<details>
<summary><b>HTTP API + Home Assistant light entity</b></summary>

This is the **same `api_port`** as the [Web dashboard](#web-dashboard), so setting it also serves the browser dashboard. The legacy `/backlight` endpoint controls the backlight remotely, and brightness set through it survives sleep/wake cycles.

```bash
curl http://<nas>:8080/backlight                          # {"state":"on","brightness":100}
curl -X POST http://<nas>:8080/backlight -d '{"state":"off"}'
curl -X POST http://<nas>:8080/backlight -d '{"brightness":50}'
```

Home Assistant `configuration.yaml` for a light entity with brightness:

```yaml
rest_command:
  nas_display_backlight:
    url: "http://<nas>:8080/backlight"
    method: POST
    content_type: "application/json"
    payload: "{{ payload }}"

rest:
  - resource: "http://<nas>:8080/backlight"
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
          data: { payload: '{"state":"on"}' }
        turn_off:
          service: rest_command.nas_display_backlight
          data: { payload: '{"state":"off"}' }
        set_level:
          service: rest_command.nas_display_backlight
          data: { payload: '{"brightness":{{ (brightness / 2.55) | round }}}' }
```

</details>

<details>
<summary><b>OPNsense page endpoints</b></summary>

| Endpoint | Shown as |
|----------|----------|
| `/api/routes/gateway/status` | Gateway RTT and status |
| `/api/core/firmware/status` | Update availability |
| `/api/dhcpv4/leases/searchLease` | Active DHCP lease count |
| `/api/unbound/overview/totals/0` | DNS queries and blocked % |
| `/api/diagnostics/traffic/top/{interface}` | WAN in/out gauges |

Create the API key in OPNsense under System, Access, Users.

</details>

## Configuration

A fresh install seeds `/etc/ug-paneld/config.json` (existing files are never overwritten on upgrade). Edit it to change the defaults:

```json
{
    "poll_rate": 2,
    "brightness": 100,
    "backlight_timeout": 30,
    "sleep_brightness": 0,
    "language": "en",
    "clock_24h": 1,
    "wallpaper": "",

    "api_port": 0,
    "api_password": "",

    "leds_on": 1,
    "led_night": 0,
    "led_night_start": "21:00",
    "led_night_end": "08:00",
    "timezone": "",

    "state_file": "",

    "debug": false
}
```

The **web dashboard is off by default** (`api_port: 0`); set a port to enable it (see [Web dashboard](#web-dashboard)). To show the OPNsense gauges, add `opnsense_url`, `opnsense_key` and `opnsense_secret` (and optionally `wan_interface` and `wan_max_mbps`). The seed file above is a starter set; the full list of keys is in the table below.

Settings you change on the display or in the web UI (brightness, timeout, wallpaper, language, LEDs, clock format, timezone) persist separately in `state.json`, so your `config.json` is never rewritten. On **Proxmox / Debian** that file lives in `/etc/ug-paneld/`. On **TrueNAS SCALE and Unraid** the system area is rebuilt every boot, so the installer points the daemon's state file at the pool or flash drive (via the `state_file` key or the `UG_PANELD_STATE` env var), and runtime changes survive a reboot there too. The fan mode is separate: it lives in `/etc/ug-fand/config`, which is also ephemeral on those platforms, but with the bundled install a mode or curve change is mirrored to the pool/flash `fand-config`, so it survives a reboot as well.

<details>
<summary><b>All config keys</b></summary>

| Key | Default | Description |
|-----|---------|-------------|
| `poll_rate` | `2` | How often to poll system stats (seconds) |
| `brightness` | `100` | Backlight brightness (1-100) |
| `backlight_timeout` | `30` | Seconds before the screen sleeps (0 = never) |
| `language` | `en` | UI language default: `en`, `de`, `es`, `fr`, `pt`, or `id`. Changing it on the panel saves to `state.json` and overrides this; set it here for a reboot-stable default (useful on TrueNAS, where `state.json` is not restored after a reboot) |
| `sleep_brightness` | `0` | Backlight % while asleep; `0` is fully off (tap-to-wake still works) |
| `clock_24h` | `1` | Panel clock format: `1` = 24h, `0` = 12h (AM/PM); editable from the web UI |
| `wallpaper` | | Active wallpaper: a built-in name, `custom`, `none`, or empty for auto (custom if uploaded, else none); editable from the panel/web |
| `leds_on` | `1` | Front LEDs on (`1`) or off (`0`) |
| `led_night` | `0` | Front-LED night mode enabled (`1` = LEDs off during the night window) |
| `led_night_start` | `21:00` | Front-LED night window start (`HH:MM`); editable from the web UI |
| `led_night_end` | `08:00` | Front-LED night window end (`HH:MM`); editable from the web UI |
| `timezone` | | Panel time zone, for example `Europe/Berlin` (empty = system default); editable from the web UI. Affects the panel clock and night window only, not the system |
| `api_port` | `0` | Web dashboard and control API port (0 = disabled). See [Web dashboard](#web-dashboard) |
| `api_password` | | Web dashboard password (empty = controls open on LAN; restart/shutdown always require it) |
| `force_shutdown` | `false` | **Proxmox only.** `false` (default) does a plain host poweroff on a panel/web/button shutdown. `true` first asks each running VM/CT to stop (`qm`/`pct`) and force-stops any still running after `guest_shutdown_timeout`, so a hung guest cannot wedge the shutdown. No effect on hosts without `qm`/`pct` |
| `guest_shutdown_timeout` | `90` | **Proxmox only.** Grace period (seconds) per guest before it is force-stopped; used only when `force_shutdown` is `true` |
| `power_button` | `auto` | Chassis power button handling. `auto` grabs the ACPI power button so the daemon owns it (logind resumes if ug-paneld exits); `off` leaves it to logind; or a specific `/dev/input/eventN` |
| `boot_settle_secs` | `120` | Cold-boot settle: re-assert the backlight and hold off the idle timeout until the EC accepts it (panel lit), capped at this many seconds of uptime; 0 = off |
| `state_file` | | Where panel/web settings are persisted; empty = `/etc/ug-paneld/state.json`. On TrueNAS/Unraid the installer points this (or the `UG_PANELD_STATE` env var) at the pool/flash so runtime changes survive a reboot |
| `storage_path` | `/` | Mountpoint the Storage widget reports usage for. On TrueNAS the root is the read-only boot pool, so set this to a data pool (for example `/mnt/tank`) for useful numbers. `statvfs` of a pool mountpoint covers the whole pool, regardless of how many drives back it |
| `drm_device` | auto | DRM device path, for example `/dev/dri/card0`; empty scans all (legacy key `drm_card` works) |
| `connector` | `auto` | DRM connector: a name (`eDP-1`), numeric id, or `auto` |
| `drm_probe_timeout` | `60` | Seconds to wait at startup for a connected connector (high so the early-boot start waits for the panel instead of giving up) |
| `i2c_device` | `auto` | Touch ACPI id to unbind from its HID-over-I2C driver (whatever owns it, such as `i2c_hid_acpi` or `i2c_hid`): `auto` (knows `CUST0000:00` and `MSFT8000:00`), `none`, or a specific id |
| `touch_device` | `auto` | Touch I2C bus: `auto` resolves it from the ACPI link; an explicit `/dev/i2c-2` works |
| `touch_probe_timeout` | `10` | Seconds to wait at startup for the touch I2C bus to appear before disabling touch (`0` = single attempt, no wait); raise it if an add-in GPU delays I2C init at boot |
| `debug` | `false` | Verbose logging (DRM probe and raw touch frames once per second); leave off in normal use |
| `opnsense_url` / `_key` / `_secret` | | OPNsense API (empty = page disabled) |
| `wan_interface` | `wan` | OPNsense interface for the WAN gauges |
| `wan_max_mbps` | `1000` | Scales the WAN arc gauges |

</details>

## Troubleshooting

> [!WARNING]
> **Never diagnose the touch controller with ug-paneld stopped.** The chip auto-sleeps when nobody polls it and then answers every I2C transaction with constant `0x23` bytes, which is indistinguishable from a broken chip. The running daemon's 33 to 50 ms polling keeps it awake (that is also why tap-to-wake works with the backlight fully off).

**Display black, service exits with code 2** ("No connected DRM connector found"): the kernel never brought up the internal panel. On newer revisions i915 cannot read the eDP panel over the AUX channel on a cold boot and disables it, so apply the one-time UGOS init from the [Install](#install) section. The tell-tale is the `eDP-1` connector being absent entirely (only the external `DP-*` outputs remain). Useful checks:

```bash
dmesg | grep -iE 'eDP|DDI A|link'   # look for "failed to retrieve link info, disabling eDP"
ls /sys/class/drm/                  # is there an eDP-1 at all? (newer Pro: often only DP-* until UGOS init)
ls /sys/bus/i2c/devices/            # CUST0000 or MSFT8000 revision?
journalctl -u ug-paneld -n 100      # ug-paneld's own connector/probe inventory
```

**Alternative fix (UEFI boot path).** On some newer units the UGOS step does not help, because the real cause is the UEFI firmware graphics handoff. What has fixed it on real hardware, while staying in UEFI: in the BIOS boot order, pick the **Linux Boot Manager** entry (systemd-boot) instead of a generic **UEFI OS** entry.

<details>
<summary><b>Background: why newer revisions boot with a dead panel</b></summary>

The BIOS declares the panel correctly (a healthy VBT: eDP on DDI-A/AUX-A, 1 lane, 258×960), and it is a standard Intel eDP display that i915 drives directly through the PCH panel power sequencer. The catch is the panel's sink (its TCON): on a cold Linux boot it does not answer the first DPCD read over the AUX channel, so i915 concludes the port is a ghost, logs `failed to retrieve link info, disabling eDP`, and removes the connector entirely. That is why the panel appears as no `eDP-1` connector at all (only the external `DP-*` outputs are listed), rather than a connected-but-black screen.

Booting UGOS once initializes the panel's sink so it answers on AUX. That initialization is non-volatile: it has been verified to survive later reboots and a full mains-off power cut, so a Linux/Proxmox boot afterwards inherits a panel that responds and i915 keeps the `eDP-1` connector. It is genuinely a one-time step per unit; repeat it only after a firmware update or if the panel is ever reset. (An earlier theory that an EC power rail was switched off turned out to be wrong: the panel's VDD pins read identically on a working and a dark unit, so the only difference is whether the eDP sink has been initialized.)

There is also an in-flight Intel kernel patch for exactly this Meteor Lake case, which wakes the eDP sink with a DP power-on write before the first DPCD read. It is not in released kernels yet, but a future kernel may let the panel come up under Linux without the UGOS step.

Newer revisions also enumerate the touchscreen as `MSFT8000:00` instead of `CUST0000:00`, and ug-paneld knows both ids. The DRM card number can change between boots (`card0` or `card1`), so ug-paneld scans all of them.

If a connector **is** connected but the screen stays black, set `"debug": true`, restart, and read the journal: every device, connector and mode decision is logged.

</details>

**Text console on an external monitor is tiny (front panel and big screen together).** With the front panel and an external HDMI/DP monitor both connected, the Linux text console sizes itself to the smallest display, so the big monitor only shows a tiny 258×960 console in the corner. This is not ug-paneld: the two screens sit on separate display pipes, and it is simply how the in-kernel framebuffer console clones. The opt-in `tools/setup-nondesktop-console.sh` marks the front panel as a "non-desktop" display (the same flag the kernel already uses for VR headsets and the Apple Touch Bar) via a per-unit EDID override, so the console ignores the panel and uses the external monitor at its full resolution. ug-paneld keeps driving the panel normally, since it selects the connector directly and never reads the EDID identity. Proxmox/Debian only, run as root, then reboot to apply (`--uninstall` reverts). One caveat: with the panel flagged non-desktop and no external monitor attached at boot, there is no local text console until one is plugged in (the front panel and SSH still work).

## How it works

Everything runs in userspace, with no display kernel patches:

| Component | Chip | Interface | Driven via |
|-----------|------|-----------|------------|
| Display | eDP panel, 258×960 ARGB | DRM | standard Linux DRM, device/connector auto-detected |
| Backlight | ITE IT55xx embedded controller | x86 port I/O (`0x62`/`0x66`) | `iopl(3)` plus `outb`/`inb` |
| Touch | AXS15231B-compatible (`CUST0000:00` / `MSFT8000:00`) | I2C @ `0x3b` | bus resolved from the ACPI device link |
| Front LEDs | Holtek HT32F52231 MCU | I2C @ `0x3a` (SMBus) | [LED setup](#front-panel-leds) |

<details>
<summary><b>Backlight EC protocol (with test snippet)</b></summary>

1. Wait for the Input Buffer Full (IBF) flag to clear on port `0x66`.
2. Write `0x81` to `0x66` (EC write-memory command).
3. Wait for IBF clear, then write `0xA5` to `0x62` (backlight address).
4. Wait for IBF clear, then write the brightness value to `0x62`.

Brightness is inverted: `1` is full, `198` is off. Test from the shell:

```bash
python3 -c "
f=open('/dev/port','r+b',buffering=0)
import time
def wb(p,v): f.seek(p); f.write(bytes([v]))
def rb(p): f.seek(p); return f.read(1)[0]
def wait():
    for _ in range(5000):
        if not (rb(0x66) & 0x02): return
        time.sleep(0.0001)
wait(); wb(0x66,0x81); wait(); wb(0x62,0xA5); wait(); wb(0x62,198)  # off
"
```

The same EC handles fan, watchdog and power in UGOS' proprietary `ug_idx6011pro-sio.ko`. ug-paneld only touches the backlight register.

</details>

<details>
<summary><b>Touch protocol (AXS15231B)</b></summary>

Same controller family as some ESP32 boards. The protocol matches [ESPHome's AXS15231B driver](https://api-docs.esphome.io/axs15231__touchscreen_8cpp_source).

Read command (8 bytes): `0xB5 0xAB 0xA5 0x5A 0x00 0x00 0x00 0x08`

```c
uint8_t  num = byte[1] & 0x0F;                  /* active contacts; 0 = none (incl. the 0x90 idle fill) */
uint8_t  event = byte[2] >> 6;                  /* 0=down, 1=up, 2=contact */
uint16_t x = (byte[2] & 0x0F) << 8 | byte[3];   /* 12-bit, 0-257 */
uint16_t y = (byte[4] & 0x0F) << 8 | byte[5];   /* 12-bit, 0-959 */
```

A frame is a real touch only when `num != 0`. The idle stream is a constant `0x90` fill (`num == 0`), which ug-paneld rejects (the v1.6.1 phantom-touch fix).

Probe from the shell (with `i2c-hid-acpi` unbound):

```bash
i2ctransfer -y 2 w8@0x3b 0xB5 0xAB 0xA5 0x5A 0x00 0x00 0x00 0x08 r8
```

</details>

## Build from source

```bash
apt install build-essential libdrm-dev libcurl4-openssl-dev pkg-config
git clone --recursive https://github.com/Reevoy24/ugreen-idx6011-panel
cd ugreen-idx6011-panel
make default                   # binary: ./ug-paneld   (requires root to run)
make fand                      # fan daemon only: ./ug-fand  (libc only, no display deps)
./build-deb.sh                 # .deb packages (panel; version read from include/version.h)
./build-tarballs.sh            # TrueNAS/Unraid tarballs (panel)
./build-fand.sh                # display-free fan-only build: .deb plus TrueNAS/Unraid tarballs
./build-mockups.sh             # render all pages as PNGs (no hardware needed)
```

All three build scripts read the version from `include/version.h` (the panel scripts from `UG_VERSION`, `build-fand.sh` from `UG_FAND_VERSION`). Pass an explicit version as the first argument to override it.

To run a self-built binary manually: the `i2c-hid-acpi` module grabs the touchscreen on boot. ug-paneld unbinds it automatically and logs what it did (the `.deb` blacklists it instead). For boot-start, install `ug-paneld.service` into `/etc/systemd/system/`.

## Credits

* [Adam Conway's original ug-paneld](https://github.com/Incipiens/ugreen-idx6011-pro-nas-display), the project this repo was forked from
* [klein0r/ugreen_leds_controller](https://github.com/klein0r/ugreen_leds_controller), the iDX6011 Pro LED protocol and driver fork, building on [miskcoo/ugreen_leds_controller](https://github.com/miskcoo/ugreen_leds_controller) and the findings in [issue #93](https://github.com/miskcoo/ugreen_leds_controller/issues/93)
* [ESPHome AXS15231B driver](https://api-docs.esphome.io/axs15231__touchscreen_8cpp_source), the touchscreen protocol reference
* [LVGL](https://lvgl.io), the UI toolkit rendering the dashboard (MIT)
* Fonts: [Montserrat](https://fonts.google.com/specimen/Montserrat) (SIL OFL 1.1) and [Font Awesome Free](https://fontawesome.com/license/free) symbols, embedded as bitmap fonts exactly like LVGL's built-ins
