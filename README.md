# ugreen-idx6011-panel

Touch dashboard, front-LED and fan control for the UGREEN NASync iDX6011 Pro — plus standalone fan control for the non-Pro iDX6011 — on Proxmox, Debian, TrueNAS SCALE and Unraid.
*Community project — not affiliated with or endorsed by UGREEN.*

[![Release](https://img.shields.io/badge/release-v1.7.0-2ea44f)](../../releases/latest)
![Platforms](https://img.shields.io/badge/runs%20on-Proxmox%20·%20Debian%20·%20TrueNAS%20·%20Unraid-6f42c1)
![Field-tested](https://img.shields.io/badge/field--tested%20on-Proxmox%20VE-success)
![UI](https://img.shields.io/badge/UI-LVGL%209-ff6d00)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

The iDX6011 Pro has a 258×960 touch display on the front. Under UGOS it shows
system stats; under Proxmox, Debian, TrueNAS or Unraid it stays black and
the 9 front status LEDs cycle a rolling demo animation forever.

**ug-paneld** fixes both: a UGOS-style touch dashboard driven entirely from
userspace (no proprietary backend, no display kernel patches), plus a
complete [front-LED setup](#front-panel-leds).

![All dashboard pages](images/pages-overview.png)

**Jump to:** [Install](#install) · [Front panel LEDs](#front-panel-leds) · [Fan control](#fan-control) · [Configuration](#configuration) · [Troubleshooting](#troubleshooting)

## Highlights

- 🖥️ **Swipeable dashboard** — Home (clock, CPU ring, glass tiles), Hardware,
  Network, Disks, Proxmox guests (PVE hosts only), OPNsense (optional)
- 👆 **Touch works** — swipe between pages, pull-down settings panel with
  brightness, screen-off timeout, wallpaper, language (EN/DE/ES/FR/PT/ID),
  restart/shutdown
- 🌀 **Fan monitoring + control** — swipe **left** from Home for live RPM and
  temps, **Silent / Default / Turbo** modes and the live curve; the bundled
  `ug-fand` daemon drives the ITE EC from userspace (no kernel module). Also
  ships as a **standalone, display-free build** that auto-detects the **non-Pro
  iDX6011** (2 system fans)
- 🌐 **Web dashboard** — opt-in (`api_port`): ug-paneld serves a browser UI on
  the LAN that mirrors the whole panel — stats, fan control with curve editor,
  all settings, 12h/24h clock + timezone, wallpaper upload, restart/shutdown
- 💡 **Front LED control** — stops the rolling animation; disk activity +
  SMART health + network blinking; LED toggle and **night mode**
  (21:00–08:00, configurable) right on the display
- 🌙 **Screen sleep** — black frame + backlight off after idle, tap to wake
- 🎨 **Wallpapers** behind every page (3 built-ins + your own PNG)
- 🏠 **Home Assistant** — optional HTTP API exposes the backlight as a light
  entity
- 📦 Packaged for **Proxmox/Debian** (.deb), **TrueNAS SCALE** and
  **Unraid** (tarballs) — plus a separate **display-free fan-only** build for
  headless / non-Pro units

## Install

Grab the latest packages from the [releases page](../../releases).

> [!NOTE]
> **No display (non-Pro iDX6011)?** The packages below are the **panel**
> — they're for the **iDX6011 Pro** and drive the front display. For a headless or
> **non-Pro** unit, install the separate **display-free, fan-only** build instead:
> [**ug-fand v1.0.0**](https://github.com/Reevoy24/ugreen-idx6011-panel/releases/tag/ug-fand-v1.0.0)
> (`.deb` for Proxmox, tarballs for TrueNAS / Unraid). See [Fan control](#fan-control).

**Proxmox / Debian** (and other systemd distros):

```bash
dpkg -i ug-paneld_*_amd64.deb        # installs, enables + starts the service
```

> A `no-blacklist` variant exists if you prefer to handle the `i2c-hid-acpi`
> module yourself.

**TrueNAS SCALE** (Linux-based; CORE is not supported) — installs to a pool,
starts via an auto-registered Post-Init script:

```bash
tar xzf ug-paneld_*_truenas_amd64.tar.gz && cd ug-paneld
sh install.sh /mnt/<your-pool>/ug-paneld
```

**Unraid** — persists on the flash drive, hooks into `/boot/config/go`:

```bash
tar xzf ug-paneld_*_unraid_amd64.tar.gz && cd ug-paneld
sh install.sh
```

> Every package **bundles fan control**: installing also installs and starts the
> `ug-fand` daemon, so the [Fan control](#fan-control) page works out of the box
> — no separate step.

> [!NOTE]
> The display, dashboard, front LEDs and fan control are **field-tested on
> Proxmox VE on real hardware** (a newer-revision iDX6011 Pro). On **Unraid** the display
> works, but the **touchscreen and the front LEDs likely do not**: stock Unraid's kernel
> omits the Intel SoC I2C stack this Meteor Lake hardware needs — `intel_lpss` for the
> touch's DesignWare bus, and the pinctrl/SMBus pieces for the i801 bus the LED MCU sits
> on — so neither I2C bus comes alive. The display itself is unaffected (it runs over
> i915/DRM, not I2C) and the dashboard stays on and readable. The dead SMBus was reported
> on a non-Pro iDX6011 (`i2cdetect` finds nothing, the MCU at `0x3a` stays silent) and
> matches our own touch diagnosis; we haven't re-confirmed the LED side on Unraid
> ourselves. The real fix is a custom Unraid kernel with `CONFIG_PINCTRL_METEORLAKE` +
> `CONFIG_MFD_INTEL_LPSS_*`, not the LED driver. See
> [`packaging/unraid/README.txt`](packaging/unraid/README.txt) for details and the
> custom-kernel workaround. The TrueNAS SCALE **panel**
> package ships the identical binaries, but the display/touch/LED side hasn't been
> run on that platform yet — feedback (good or bad) via the issues is welcome.
> **Fan control, though, is verified on TrueNAS SCALE** (non-Pro iDX6011: install,
> reboot persistence and Silent/Default/Turbo switching all confirmed).

> [!IMPORTANT]
> **Display stays black?** On newer iDX6011 Pro revisions the panel power is
> switched by the embedded controller and vanilla Linux doesn't know how.
> The one-time fix: boot UGOS once, then **reboot** (don't power off) into
> your Linux drive via the firmware boot menu (F11/F12). The EC keeps the
> panel powered persistently — verified to survive reboots, shutdowns and
> mains cuts. Details in [Troubleshooting](#troubleshooting).

## Front panel LEDs

The 9 RGB status LEDs (power, 2× LAN, 6× disk) are driven by a separate MCU
with a protocol that differs from older UGREEN models — reverse-engineered in
[miskcoo/ugreen_leds_controller#93](https://github.com/miskcoo/ugreen_leds_controller/issues/93)
and implemented in
[klein0r's fork](https://github.com/klein0r/ugreen_leds_controller) (all
credit to them).

**Proxmox / Debian** — full setup (kernel module via DKMS, live activity +
SMART health colors). Run as root on the **host**:

```bash
wget https://raw.githubusercontent.com/Reevoy24/ugreen-idx6011-panel/master/tools/setup-ugreen-leds.sh
bash setup-ugreen-leds.sh
```

The rolling animation stops immediately; disk LEDs show activity and health,
LAN LEDs blink on traffic. The script installs the kernel-header meta package
(`proxmox-default-headers`), so DKMS rebuilds the module on every kernel update
and the LEDs keep working across reboots and upgrades. (If the LEDs ever drop to
a plain static state after a kernel update, the matching headers were missing —
just re-run the script to repair it.)
With the setup installed, the ug-paneld settings panel gains a **Status LEDs
on/off row and a night mode row** (LEDs off automatically between
`led_night_start` and `led_night_end`, default 21:00–08:00 — turning them on
during the window overrides it until the window ends).

**TrueNAS SCALE** — ready-made tarballs (`ugreen-leds_*`) from the
[releases page](../../releases): same one-command install pattern as above.
They stop the animation at boot, set a calm base state and run a small
userspace activity monitor (busy = hardware blink, idle = solid; no kernel
module, survives every platform update). Per-I/O triggers with SMART colors
need the kernel module built for that kernel — tracked upstream in
[0x556c79/install_ugreen_leds_controller#23](https://github.com/0x556c79/install_ugreen_leds_controller/issues/23).
*(LED side not yet confirmed on TrueNAS hardware — feedback welcome.)*

**Unraid** — reported **not working** on the iDX6011: the front-LED MCU lives
on the i801 SMBus, and stock Unraid's kernel doesn't bring that bus up on this
Meteor Lake hardware (the same missing Intel SoC I2C/pinctrl stack that disables
touch — see the note above), so neither the kernel driver nor our userspace tool
can reach the MCU. The fix is a custom kernel, not the LED driver: the upstream
plugin issue
[ich777/unraid-ugreenleds-driver#8](https://github.com/ich777/unraid-ugreenleds-driver/issues/8)
is closed (the driver is still compiled, but the plugin won't be updated), so a
plugin fork won't help here.

<details>
<summary><b>LED notes: bay order, LAN order, manual control</b></summary>

- **Bay order:** the default ata-based disk→LED mapping is not yet verified
  on the iDX6011 Pro. Generate I/O on one disk and check that the right bay
  blinks; if the order is wrong, run `ugreen-detect-disks` and switch
  `/etc/ugreen-leds.conf` to `MAPPING_METHOD=serial`.
- **LAN port order:** if LAN1/LAN2 are swapped, set
  `NETLED_IFACES="<nic1> <nic2>"` in `/etc/default/ugreen-idx-netled`
  (Proxmox) or `NICS="..."` in `ugreen-leds-mon.conf` (TrueNAS/Unraid).
- **Manual control:** the CLI tool and the kernel module conflict — stop the
  LED services and `rmmod led_ugreen` before using `ugreen_leds_cli` by hand.
- **LED toggle semantics:** "off" on the display stops `ugreen-diskiomon`
  and zeroes every LED; "on" restarts the monitors. Persists in
  `state.json`.

</details>

## Fan control

The iDX6011's fans hang off an **ITE IT55xx embedded controller** reachable as a
standard ACPI EC (ports `0x62`/`0x66`). UGOS drives them through a proprietary
kernel module, so a stock Linux (Proxmox, TrueNAS, Debian, Unraid) sees no fan
sensors or control at all. **`ug-fand`** restores both — monitoring **and**
control — entirely from userspace, no kernel module. It **auto-detects the
model**: the **iDX6011 Pro** has 4 fans (CPU + system); the **non-Pro
iDX6011** has 2 system fans (no separate CPU fan — the system fans
cool the CPU too) at different EC offsets.

> ⚠️ The bundled curves are conservative starting points; writing fan registers
> can overheat the NAS if a curve is wrong, so verify on your hardware.

> **Non-Pro / no display?** The panel packages in [Install](#install) are for the
> Pro (they drive the display). The non-Pro iDX6011 has no panel —
> install the **display-free fan-only build** instead:
> [**ug-fand v1.0.0**](https://github.com/Reevoy24/ugreen-idx6011-panel/releases/tag/ug-fand-v1.0.0)
> — a `.deb` for Proxmox and tarballs for TrueNAS / Unraid, config persists across
> reboots and OS updates. Everything below applies to it too.

### Setup

Nothing to do — `ug-fand` is **bundled with the package** and started
automatically by the [install](#install) above (the `.deb` ships it as a systemd
service; the TrueNAS and Unraid tarballs install and launch it too). Confirm it
is running:

```bash
systemctl status ug-fand          # Proxmox / Debian
cat /var/log/ug-fand.log          # TrueNAS / Unraid
```

<details open>
<summary><b>Standalone install (building from source / daemon only)</b></summary>

Build just the daemon (`make fand` → `ug-fand` at the repo root), then:

```bash
# Proxmox / Debian (systemd) or Unraid (/boot/config + go hook) — auto-detected
sudo sh packaging/fand/install.sh

# TrueNAS SCALE (installs onto a pool + registers a Post-Init script)
sudo sh packaging/fand/install.sh /mnt/<pool>/ug-fand
```

</details>

Config — `/etc/ug-fand/config`:

```
mode=default       # silent | default | turbo
interval=3         # seconds between updates
# optional per-mode curves — comma-separated temp:speed points (°C : 0-100%):
cpu_default=0:15,60:15,70:38,78:71,86:100
sys_default=0:28,52:28,58:55,63:80,68:100
```

The three modes are temperature→speed curves (silent = quietest … turbo =
coolest). `cpu_*` curves follow the CPU temperature, `sys_*` the disk/NVMe
temperature. The temperature is smoothed and a speed deadband is applied, so the
fans hold a steady speed instead of hunting on brief CPU spikes. A thermal
failsafe forces full speed above the critical thresholds (88 °C CPU, 68 °C
disks), and a missing temperature reading is treated as "full" — a broken sensor
never silences the fans.

### Adjusting the curves

Each curve is a comma-separated list of `temp:speed` points — temperature in °C,
fan speed in percent (`0`–`100`, where `100` = full). Edit them in
`/etc/ug-fand/config` with any editor:

```bash
sudo nano /etc/ug-fand/config      # or: vi /etc/ug-fand/config
```

Change the numbers and **save** — `ug-fand` reloads the file automatically, no
restart needed. Then check the result:

```bash
cat /run/ug-fand/status            # mode, temps, RPM, and applied speed %
```

Lower the speed numbers (or push the ramp temperatures higher) for a quieter
NAS; raise them for a cooler one. For example, to keep the system fans at their
quiet floor until the disks get warmer, widen the flat part of the curve:

```
sys_default=0:28,56:28,60:55,64:80,68:100   # quiet up to 56 °C, then ramp
```

Delete a curve line to fall back to the built-in default.

> On TrueNAS / Unraid the rootfs is rebuilt every boot, so `/etc/ug-fand/config`
> is re-synced at boot from the copy on your pool/flash. With the **bundled
> install**, a mode or curve change made on the panel or in the web UI is mirrored
> back to that copy (`<install-dir>/fand-config`), so it survives a reboot. For the
> **standalone fan-only daemon** (no panel), edit the pool copy directly —
> `/mnt/<pool>/ug-fand/config` — and re-run its `start.sh`.

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

Same EC as the backlight: read a byte = command `0x80`, write a byte = command
`0x81`, address on `0x62` (wait for IBF = `0x66` bit `0x02` to clear before each
write; OBF = bit `0x01` set before a read).

**Tachometer — read, 16-bit big-endian = RPM:**

| Fan | hi / lo |
|-----|---------|
| cpufan1 | `0x34` / `0x35` |
| cpufan2 | `0x36` / `0x37` |
| sysfan1 | `0x38` / `0x39` |
| sysfan2 | `0x3A` / `0x3B` |

**Duty — write; per fan: enable byte = `1`, then duty `0..198`:**

| Fan | enable / duty |
|-----|---------------|
| cpufan1 | `0xB0` / `0xB1` |
| cpufan2 | `0xB2` / `0xB3` |
| sysfan1 | `0xB4` / `0xB5` |
| sysfan2 | `0xB6` / `0xB7` |

The above is the **iDX6011 Pro**. The **non-Pro iDX6011** uses the same
EC and protocol but only 2 system fans, at different offsets (duty still `0..198`):

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

## Web UI

Opt-in: set `api_port` in the config and ug-paneld serves a browser dashboard on
the LAN that **mirrors the whole panel** — live stats (CPU/RAM/temps/uptime,
network, disks, Proxmox, OPNsense, GPU, fans), the fan **Silent / Default /
Turbo** switch and curve, and every setting (brightness, screen-off timeout,
sleep brightness, language, LEDs + night window, wallpaper incl. **custom
upload**) plus **restart/shutdown**. It's built into ug-paneld — no extra
service or container.

```json
{
    "api_port": 8080,
    "api_password": "choose-a-password"
}
```

Restart ug-paneld, then open `http://<nas-ip>:8080`.

- **Monitoring is open** on the LAN; **changing settings/fan** requires the
  password when `api_password` is set; **restart/shutdown always require** it
  (and are refused entirely when no password is set).
- The legacy `GET/POST /backlight` endpoint still works (Home Assistant — see
  below).

> [!WARNING]
> This is a control surface on a daemon running as root, over plain HTTP. **LAN
> only — never port-forward it to the internet.** Set `api_password` and keep it
> on a trusted network. (ug-fand's thermal failsafe still forces full speed above
> the critical thresholds, so a bad curve can't overheat the NAS.)

## Configuration

The package installs this default `/etc/ug-paneld/config.json` (a fresh install
seeds it; existing files are never overwritten on upgrade) — edit it to change
the defaults:

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

The **web dashboard is off by default** (`api_port: 0`) — set a port to enable
it (see [Web UI](#web-ui)). To show the OPNsense gauges, add
`opnsense_url`/`opnsense_key`/`opnsense_secret` (and optionally
`wan_interface`/`wan_max_mbps`). Every key is in the table below.

Settings changed on the display or in the web UI (brightness, timeout, wallpaper,
language, LEDs, clock format, timezone …) persist separately in `state.json` —
your `config.json` is never rewritten. On **Proxmox / Debian** that lives in
`/etc/ug-paneld/`. On **TrueNAS SCALE / Unraid** `/etc` is rebuilt every boot, so
the installer points the daemon's state file at the pool/flash (via the
`state_file` key / `UG_PANELD_STATE` env) — runtime changes survive a reboot
there too. (The fan **mode** lives in `/etc/ug-fand/config`, which is ephemeral on
those platforms; with the bundled install a mode/curve change on the panel or web
UI is mirrored to the pool/flash `fand-config`, so it survives a reboot too.)

<details>
<summary><b>All config keys</b></summary>

| Key | Default | Description |
|-----|---------|-------------|
| `poll_rate` | `2` | How often to poll system stats (seconds) |
| `brightness` | `100` | Backlight brightness (1-100) |
| `backlight_timeout` | `30` | Seconds before the screen sleeps (0 = never) |
| `language` | `en` | UI language default: `en`, `de`, `es`, `fr`, `pt`, or `id`. Changing the language on the panel itself is saved to `state.json` and overrides this; set it here for a reboot-stable default (e.g. on TrueNAS, where `state.json` is not restored after a reboot) |
| `sleep_brightness` | `0` | Backlight % while asleep; `0` = fully off (tap-to-wake keeps working) |
| `clock_24h` | `1` | Panel clock format: `1` = 24h, `0` = 12h (AM/PM); editable from the web UI |
| `wallpaper` | | Active wallpaper: a built-in name, `custom`, `none`, or empty = auto (custom if uploaded, else none); editable from the panel/web |
| `leds_on` | `1` | Front LEDs on (`1`) or off (`0`) |
| `led_night` | `0` | Front-LED night mode enabled (`1` = dim/off during the night window) |
| `led_night_start` | `21:00` | Front-LED night window start (`HH:MM`); editable from the web UI |
| `led_night_end` | `08:00` | Front-LED night window end (`HH:MM`); editable from the web UI |
| `timezone` | | Panel time zone, e.g. `Europe/Berlin` (empty = system default); editable from the web UI. Affects the panel clock + night window only, not the system |
| `api_port` | `0` | Web dashboard + control API port (0 = disabled). See [Web UI](#web-ui) |
| `api_password` | | Web dashboard password (empty = controls open on LAN; restart/shutdown always require it) |
| `force_shutdown` | `false` | **Proxmox only.** `false` (default) = a panel/web/button shutdown or reboot does a plain host poweroff. `true` = first gracefully stop each running VM/CT (`qm`/`pct`) and **force-stop** any still running after `guest_shutdown_timeout`, *before* powering off the host — so a hung guest can't wedge the shutdown. On TrueNAS/Unraid/Debian (no `qm`/`pct`) it has no effect, just a normal poweroff |
| `guest_shutdown_timeout` | `90` | **Proxmox only.** Grace period (seconds) per guest before it is force-stopped — only used when `force_shutdown` is `true` |
| `power_button` | `auto` | The chassis **power button** triggers the shutdown above. `auto` grabs the ACPI Power Button (so systemd-logind doesn't also act; if ug-paneld exits, logind's default poweroff resumes); `off` leaves the key to logind; or a specific `/dev/input/eventN` |
| `boot_settle_secs` | `120` | Cold-boot settle: re-assert the backlight and hold off the idle timeout until the EC accepts it (panel lit), capped at this many seconds of uptime; 0 = off |
| `state_file` | | Where panel/web settings are persisted; empty = `/etc/ug-paneld/state.json`. On TrueNAS/Unraid the installer points this (or the `UG_PANELD_STATE` env var) at the pool/flash so runtime changes survive a reboot |
| `drm_device` | auto | DRM device path, e.g. `/dev/dri/card0`; empty = scan all (legacy key `drm_card` works) |
| `connector` | `auto` | DRM connector: name (`eDP-1`), numeric id, or `auto` |
| `drm_probe_timeout` | `60` | Seconds to wait at startup for a connected connector (high so the early-boot start waits for the panel instead of giving up) |
| `i2c_device` | `auto` | Touch ACPI id to unbind from its HID-over-I2C driver (whatever owns it — `i2c_hid_acpi`, `i2c_hid`, …): `auto` (knows `CUST0000:00` + `MSFT8000:00`), `none`, or a specific id |
| `touch_device` | `auto` | Touch I2C bus: `auto` resolves it from the ACPI link; explicit `/dev/i2c-2` works |
| `debug` | `false` | Verbose logging (DRM probe + raw touch frames once/sec); leave off in normal use |
| `opnsense_url` / `_key` / `_secret` | | OPNsense API (empty = page disabled) |
| `wan_interface` | `wan` | OPNsense interface for the WAN gauges |
| `wan_max_mbps` | `1000` | Scales the WAN arc gauges |

</details>

<details>
<summary><b>HTTP API + Home Assistant light entity</b></summary>

Set `api_port` (e.g. `9101`) to control the backlight remotely. Brightness
set through the API survives sleep/wake cycles.

```bash
curl http://<nas>:9101/backlight                          # {"state":"on","brightness":100}
curl -X POST http://<nas>:9101/backlight -d '{"state":"off"}'
curl -X POST http://<nas>:9101/backlight -d '{"brightness":50}'
```

Home Assistant `configuration.yaml` for a light entity with brightness
slider:

```yaml
rest_command:
  nas_display_backlight:
    url: "http://<nas>:9101/backlight"
    method: POST
    content_type: "application/json"
    payload: "{{ payload }}"

rest:
  - resource: "http://<nas>:9101/backlight"
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
| `/api/routes/gateway/status` | Gateway RTT + status |
| `/api/core/firmware/status` | Update availability |
| `/api/dhcpv4/leases/searchLease` | Active DHCP lease count |
| `/api/unbound/overview/totals/0` | DNS queries / blocked % |
| `/api/diagnostics/traffic/top/{interface}` | WAN in/out gauges |

Create the API key in OPNsense under System → Access → Users.

</details>

## How it works

Everything runs in userspace — no display kernel patches:

| Component | Chip | Interface | Driven via |
|-----------|------|-----------|------------|
| Display | eDP panel, 258×960 ARGB | DRM | standard Linux DRM, device/connector auto-detected |
| Backlight | ITE IT55xx embedded controller | x86 port I/O (`0x62`/`0x66`) | `iopl(3)` + `outb`/`inb` |
| Touch | AXS15231B-compatible (`CUST0000:00` / `MSFT8000:00`) | I2C @ `0x3b` | bus resolved from the ACPI device link |
| Front LEDs | Holtek HT32F52231 MCU | I2C @ `0x3a` (SMBus) | [LED setup](#front-panel-leds) |

<details>
<summary><b>Backlight EC protocol (+ test snippet)</b></summary>

1. Wait for the Input Buffer Full (IBF) flag to clear on port `0x66`
2. Write `0x81` to `0x66` (EC write-memory command)
3. Wait for IBF clear, write `0xA5` to `0x62` (backlight address)
4. Wait for IBF clear, write the brightness value to `0x62`

Brightness is inverted: `1` = full, `198` = off. Test from the shell:

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

The same EC handles fan/watchdog/power in UGOS' proprietary
`ug_idx6011pro-sio.ko`; ug-paneld only touches the backlight register.

</details>

<details>
<summary><b>Touch protocol (AXS15231B)</b></summary>

Same controller family as some ESP32 boards — the protocol matches
[ESPHome's AXS15231B driver](https://api-docs.esphome.io/axs15231__touchscreen_8cpp_source).

Read command (8 bytes): `0xB5 0xAB 0xA5 0x5A 0x00 0x00 0x00 0x08`

```c
uint8_t  event = byte[2] >> 6;                  /* 0=down, 1=up, 2=contact */
uint16_t x = (byte[2] & 0x0F) << 8 | byte[3];   /* 12-bit, 0-257 */
uint16_t y = (byte[4] & 0x0F) << 8 | byte[5];   /* 12-bit, 0-959 */
uint8_t  id = byte[4] >> 4;                     /* touch point id */
```

Probe from the shell (with `i2c-hid-acpi` unbound):

```bash
i2ctransfer -y 2 w8@0x3b 0xB5 0xAB 0xA5 0x5A 0x00 0x00 0x00 0x0E r14
```

</details>

## Troubleshooting

> [!WARNING]
> **Never diagnose the touch controller with ug-paneld stopped.** The chip
> auto-sleeps when nobody polls it and then answers every I2C transaction
> with constant `0x23` bytes — indistinguishable from a broken chip. The
> running daemon's 33–50 ms polling keeps it awake (that's also why
> tap-to-wake works with the backlight fully off).

**Display black, service exits with code 2** ("No connected DRM connector
found"): the kernel never brought up the panel. On newer revisions the panel
power rail is switched by the EC — apply the UGOS warm-boot fix from the
[Install](#install) section. Useful checks:

```bash
for x in /sys/class/drm/card*-*/status; do echo "$x: $(cat $x)"; done   # one "connected"?
ls /sys/bus/i2c/devices/            # CUST0000 or MSFT8000 revision?
journalctl -u ug-paneld -n 100      # full DRM probe inventory is logged
```

<details>
<summary><b>Background: why newer revisions boot with a dead panel</b></summary>

The BIOS declares the panel correctly (healthy VBT: eDP on DDI-A/AUX-A,
1 lane, 258×960), but the panel's power rail is controlled by the ITE EC,
not the Intel PCH. Unpowered, the panel never answers on the AUX channel, so
i915 logs `failed to retrieve link info, disabling eDP` and gives up. UGOS
powers the rail through its proprietary EC driver; vanilla Linux doesn't
know it has to. The EC stores the power flag persistently — hence the
one-time UGOS warm-boot fix (verified across reboots, shutdowns and a
multi-minute mains cut; repeat only after an EC reset or firmware update).

Newer revisions also enumerate the touchscreen as `MSFT8000:00` instead of
`CUST0000:00`; ug-paneld knows both ids. The DRM card number can change
between boots (`card0`/`card1`) — ug-paneld scans all of them.

If a connector **is** connected but the screen stays black, set
`"debug": true`, restart, and read the journal: every device, connector and
mode decision is logged.

</details>

## Build from source

```bash
apt install build-essential libdrm-dev libcurl4-openssl-dev pkg-config
git clone --recursive https://github.com/Reevoy24/ugreen-idx6011-panel
cd ugreen-idx6011-panel
make                 # binary: ./ug-paneld   (requires root to run)
make fand                      # fan daemon only: ./ug-fand  (libc only, no display deps)
./build-deb.sh 1.4.0           # .deb packages (panel)
./build-tarballs.sh 1.4.0      # TrueNAS/Unraid tarballs (panel)
./build-fand.sh 1.0.0          # display-free fan-only build: .deb + TrueNAS/Unraid tarballs
./build-mockups.sh             # render all pages as PNGs (no hardware needed)
```

Running a self-built binary manually: the `i2c-hid-acpi` module grabs the
touchscreen on boot — ug-paneld unbinds it automatically and logs what it
did (the .deb blacklists it instead). For boot-start, install
`ug-paneld.service` into `/etc/systemd/system/`.

## Credits

- [Adam Conway's original ug-paneld](https://github.com/Incipiens/ugreen-idx6011-pro-nas-display) —
  the project this repo was forked from
- [klein0r/ugreen_leds_controller](https://github.com/klein0r/ugreen_leds_controller) —
  iDX6011 Pro LED protocol + driver fork, building on
  [miskcoo/ugreen_leds_controller](https://github.com/miskcoo/ugreen_leds_controller)
  and the findings in [issue #93](https://github.com/miskcoo/ugreen_leds_controller/issues/93)
- [ESPHome AXS15231B driver](https://api-docs.esphome.io/axs15231__touchscreen_8cpp_source) —
  touchscreen protocol reference
- [LVGL](https://lvgl.io) — the UI toolkit rendering the dashboard (MIT)
- Fonts: [Montserrat](https://fonts.google.com/specimen/Montserrat)
  (SIL OFL 1.1) and [Font Awesome Free](https://fontawesome.com/license/free)
  symbols — embedded as bitmap fonts exactly like LVGL's built-ins
