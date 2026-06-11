#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_FILE_PATH "/etc/ug-paneld/config.json"
#define DEFAULT_POLL_RATE 2
#define DEFAULT_DRM_PROBE_TIMEOUT 10

typedef struct {
    int poll_rate;
    char drm_device[64];   /* DRM device path, "" = scan /dev/dri (keys: drm_device, legacy drm_card) */
    char connector[32];    /* DRM connector name ("eDP-1"), numeric id, or "auto" */
    int drm_probe_timeout; /* seconds to wait for a connected connector before giving up */
    char i2c_device[64];   /* ACPI id to unbind from i2c_hid_acpi: "auto", "none", or e.g. "MSFT8000:00" */
    char opnsense_url[256];
    char opnsense_key[256];
    char opnsense_secret[256];
    char wan_interface[32];
    int wan_max_mbps;
    char touch_device[64];
    int brightness;
    int backlight_timeout;
    int sleep_brightness;  /* backlight %% while asleep; 0 (default) = full off —
                              the touch chip stays responsive as long as it is
                              polled, so wake-by-tap works even at full off */
    int api_port;
    int debug;             /* verbose display probe logging */
    char led_night_start[8]; /* front LED night window start, "HH:MM" */
    char led_night_end[8];   /* front LED night window end, "HH:MM" */
} config_t;

int config_load(config_t *config);

#endif
