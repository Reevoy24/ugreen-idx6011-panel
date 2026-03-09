#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_FILE_PATH "/etc/proxmox-display/config.json"
#define DEFAULT_REFRESH_RATE 2

typedef struct {
    int refresh_rate;
    char drm_card[64];
    char opnsense_url[256];
    char opnsense_key[256];
    char opnsense_secret[256];
    char wan_interface[32];
    int wan_max_mbps;
    char touch_device[64];
    int backlight_timeout;
    int api_port;
} config_t;

int config_load(config_t *config);

#endif
