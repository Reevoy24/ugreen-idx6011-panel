#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_FILE_PATH "/etc/proxmox-display/config.json"
#define DEFAULT_REFRESH_RATE 2
#define DEFAULT_FPS 30

typedef struct {
    int refresh_rate;
    int fps;
    int show_temp;
    int show_uptime;
    int use_celsius;
} config_t;

int config_load(config_t *config);

#endif
