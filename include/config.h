#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_FILE_PATH "/etc/ug-paneld/config.json"
#define DEFAULT_POLL_RATE 2
/* drive temps get their own, slower clock: off-Unraid every poll is a live
 * SMART query (drivetemp) that can audibly unpark HDD heads */
#define DEFAULT_DISK_INTERVAL 30
/* generous so an early boot start (before the panel connector is ready) waits
 * for it instead of giving up with exit code 2 */
#define DEFAULT_DRM_PROBE_TIMEOUT 60
/* the touch I2C bus can enumerate later than the display (e.g. when an external
 * GPU delays I2C init at boot); wait this long for it before disabling touch */
#define DEFAULT_TOUCH_PROBE_TIMEOUT 10
/* on Proxmox a hung guest can wedge the host shutdown: ask each running VM/CT to
 * stop gracefully, then force-stop any still running after this many seconds,
 * before powering off the host. 0 = skip guest handling (plain host poweroff) */
#define DEFAULT_GUEST_SHUTDOWN_TIMEOUT 90

typedef struct {
    int poll_rate;
    int disk_interval;     /* seconds between drive-temp polls (disk page + web).
                              On Unraid the temps come from emhttpd's disks.ini
                              (no disk I/O); elsewhere each poll is a real SMART
                              query per drive, so it runs slower than poll_rate */
    char drm_device[64];   /* DRM device path, "" = scan /dev/dri (keys: drm_device, legacy drm_card) */
    char connector[32];    /* DRM connector name ("eDP-1"), numeric id, or "auto" */
    int drm_probe_timeout; /* seconds to wait for a connected connector before giving up */
    char i2c_device[64];   /* touch ACPI id to unbind from its HID-over-I2C driver (any name): "auto", "none", or e.g. "MSFT8000:00" */
    char opnsense_url[256];
    char opnsense_key[256];
    char opnsense_secret[256];
    char wan_interface[32];
    int wan_max_mbps;
    char touch_device[64];
    int touch_probe_timeout; /* seconds to wait for the touch I2C bus to appear
                                before disabling touch; 0 = one attempt, no wait */
    int brightness;
    int backlight_timeout;
    char language[4];      /* default UI language "de"/"en"; state.json (panel) overrides */
    int sleep_brightness;  /* backlight %% while asleep; 0 (default) = full off —
                              the touch chip stays responsive as long as it is
                              polled, so wake-by-tap works even at full off */
    int clock_24h;         /* panel clock: 1 = 24h (default), 0 = 12h AM/PM */
    char wallpaper[32];    /* active wallpaper name; "" = auto (custom if present, else none) */
    int leds_on;           /* front LEDs on (1, default) or off (0) */
    int led_night;         /* front-LED night mode enabled (0 = default) */
    int api_port;
    char api_password[64]; /* web dashboard password ("" = controls open on LAN; power always needs it) */
    int debug;             /* verbose display probe logging */
    char led_night_start[8]; /* front LED night window start, "HH:MM" */
    char led_night_end[8];   /* front LED night window end, "HH:MM" */
    char timezone[40];       /* panel time zone (e.g. "Europe/Berlin"); "" = system default */
    int force_shutdown;      /* 0 (default) = panel/web/button shutdown does a
                                plain host poweroff; 1 = first gracefully stop
                                Proxmox guests (qm/pct) and FORCE-stop any still
                                running after guest_shutdown_timeout, then the
                                host — so a hung VM/CT can't wedge the shutdown */
    int guest_shutdown_timeout; /* seconds to wait for each Proxmox guest to shut
                                   down gracefully before force-stopping it (only
                                   when force_shutdown=1; no-op without qm/pct) */
    char power_button[16];   /* front power button: "auto" (default, grab the ACPI
                                Power Button and run the smart shutdown), "off"
                                (leave the key to logind), or a /dev/input/eventN */
    int boot_settle_secs;    /* cold-boot settle: re-assert the backlight and
                                hold off the idle timeout until the EC accepts a
                                write (panel lit), or at most this many seconds
                                of uptime as a hard cap; 0 = off */
    char state_file[256];    /* where runtime settings (panel/web edits) are
                                persisted; "" = default /etc/ug-paneld/state.json.
                                On TrueNAS/Unraid point this at the pool/flash so
                                changes survive a reboot (env UG_PANELD_STATE also
                                works; this key wins if set) */
    char storage_path[256];  /* mountpoint whose usage the Storage widget shows;
                                "/" (default) = the root filesystem. On TrueNAS the
                                root is the read-only boot pool, so point this at a
                                data pool, e.g. "/mnt/tank". statvfs of a pool's
                                mountpoint reports the whole pool (any drive count) */
} config_t;

int config_load(config_t *config);

#endif
