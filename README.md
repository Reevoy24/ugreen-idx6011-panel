# Ugreen Panel Daemon - Display controller for Ugreen's iDX6011 Pro

Ugreen's iDX6011 Pro NAS has a 258x960 portrait display on the front panel. Under UGOS, it shows system stats via a proprietary application (`mini_screen`) that depends on UGOS services and kernel modules.

When running Proxmox, Debian, or other Linux distributions, the display, backlight, and touchscreen don't work out of the box. However, you can use ug-paneld to provide a replacement dashboard that drives all three directly from userspace, without the proprietary UGOS backend.

## What it does

- Shows CPU, RAM, disk usage, temperature, and uptime
- Connects to an OPNsense firewall for gateway status, firmware updates, DHCP leases, DNS blocked stats, and WAN throughput
- WAN throughput shown as arc gauges
- Supports a PNG wallpaper (place at `/etc/ug-paneld/wallpaper.png`)
- Backlight turns off after a configurable idle timeout

## Hardware

The iDX6011 Pro has three hardware interfaces on the front panel. This project controls all of them from userspace without any kernel modules needed:

| Component | Chip | Interface | How we talk to it |
|-----------|------|-----------|-------------------|
| Display | MIPI DSI panel (258x960, 32bpp ARGB) | DRM | Standard Linux DRM, auto-detected |
| Backlight | ITE IT55xx Embedded Controller | x86 port I/O (`0x62`/`0x66`) | `iopl(3)` + `outb`/`inb` |
| Touchscreen | Awinic AXS15231B | I2C bus 2, address `0x3b` | `/dev/i2c-2` via `ioctl` |

## Build & Usage

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

### Run

The application requires root access (for EC port I/O and I2C access).

> [!IMPORTANT]
> The `i2c-hid-acpi` kernel module will grab the touchscreen's I2C device on boot and block direct access. You need to either unbind it before running, or blacklist it permanently.

```bash
# Option A: unbind for this session
echo "i2c-CUST0000:00" > /sys/bus/i2c/drivers/i2c_hid_acpi/unbind 2>/dev/null

# Option B: blacklist permanently
echo "blacklist i2c_hid_acpi" > /etc/modprobe.d/no-i2c-hid.conf
```

Then run:

```bash
ug-paneld
```

Press `Ctrl+C` twice to exit. The backlight turns off on shutdown.

### Start at Boot

```ini
# /etc/systemd/system/ug-paneld.service
[Unit]
Description=NAS Display Dashboard
After=multi-user.target

[Service]
Type=simple
ExecStartPre=-/bin/sh -c 'echo "i2c-CUST0000:00" > /sys/bus/i2c/drivers/i2c_hid_acpi/unbind 2>/dev/null'
ExecStart=/usr/bin/ug-paneld
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

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
    "drm_card": "",
    "touch_device": "/dev/i2c-2",
    "brightness": 100,
    "backlight_timeout": 30,
    "opnsense_url": "https://192.168.1.1:8443",
    "opnsense_key": "your-api-key",
    "opnsense_secret": "your-api-secret",
    "wan_interface": "wan",
    "wan_max_mbps": 1000
}
```

| Key | Default | Description |
|-----|---------|-------------|
| `poll_rate` | `2` | How often to poll system stats (seconds) |
| `drm_card` | auto-detect | DRM device path, e.g. `/dev/dri/card0` |
| `touch_device` | `/dev/i2c-2` | I2C bus for the touchscreen |
| `brightness` | `100` | Backlight brightness (1-100) |
| `backlight_timeout` | `30` | Seconds before backlight turns off (0 to disable) |
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

### Troubleshooting: I2C bus number

The touchscreen is on the Synopsys DesignWare I2C adapter at PCI `00:15.1`. The Linux bus number (`/dev/i2c-N`) can shift depending on driver load order. If touch isn't working:

```bash
# Find the DesignWare adapters
for i in /sys/bus/i2c/devices/i2c-*/name; do echo "$i: $(cat $i)"; done

# Scan for 0x3b on each DesignWare bus
i2cdetect -y -r 2
```

Then update `touch_device` in your config.

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
