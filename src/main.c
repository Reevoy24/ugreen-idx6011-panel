#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "lvgl/lvgl.h"
#include "include/custom_tick.h"
#include "display.h"
#include "system_stats.h"
#include "gui.h"
#include "config.h"
#include "backlight.h"
#include "opnsense.h"
#include "touch.h"
#include "api.h"

static volatile int running = 1;
static volatile int signal_count = 0;

static void signal_handler(int sig) {
    (void)sig;
    signal_count++;
    running = 0;
    if (signal_count >= 2) _exit(1);
}

// OS will display login screen on the display if bound, so we unbind first
// Works on Proxmox, assumed working on most Linux distros
static void unbind_vt_console(void) {
    int fd = open("/sys/class/vtconsole/vtcon1/bind", O_WRONLY);
    if (fd >= 0) {
        write(fd, "0", 1);
        close(fd);
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    struct sigaction sa = { .sa_handler = signal_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    unbind_vt_console();

    config_t config;
    if (config_load(&config) != 0)
        fprintf(stderr, "Warning: Failed to load config, using defaults\n");

    if (display_init(config.drm_card) != 0) {
        fprintf(stderr, "Display init failed\n");
        return 1;
    }

    backlight_init();
    backlight_on();

    if (config.api_port > 0)
        api_start(config.api_port);

    int has_touch = (touch_init(config.touch_device) == 0);
    int has_opnsense = (opnsense_init(&config) == 0);
    if (has_opnsense)
        fprintf(stderr, "OPNsense API enabled: %s\n", config.opnsense_url);

    gui_create_dashboard(has_opnsense, config.wan_max_mbps);

    uint32_t last_stats_update = 0;
    uint32_t stats_interval = config.refresh_rate * 1000;
    uint32_t bl_timeout_ms = config.backlight_timeout * 1000;
    uint32_t last_touch_time = custom_tick_get();
    int screen_asleep = 0;
    system_stats_t stats;
    opnsense_stats_t opn_stats;
    struct timespec sleep_ts = { .tv_nsec = 33000000 };

    struct timespec sleep_long = { .tv_nsec = 100000000 }; /* 100ms poll while asleep */

    while (running) {
        uint32_t now = custom_tick_get();

        if (has_touch && touch_is_pressed()) {
            last_touch_time = now;
            if (screen_asleep) {
                backlight_on();
                screen_asleep = 0;
                api_set_state(1);
            }
        }

        if (api_get_state() && screen_asleep) {
            last_touch_time = now;
            screen_asleep = 0;
        } else if (!api_get_state() && !screen_asleep) {
            screen_asleep = 1;
        }

        if (has_touch && !screen_asleep && bl_timeout_ms > 0 &&
            (now - last_touch_time >= bl_timeout_ms)) {
            backlight_off();
            screen_asleep = 1;
            api_set_state(0);
        }

        if (screen_asleep) {
            nanosleep(&sleep_long, NULL);
            continue;
        }

        gui_update_clock();

        if (now - last_stats_update >= stats_interval) {
            if (system_stats_collect(&stats) == 0)
                gui_update_dashboard(&stats);
            if (has_opnsense && opnsense_collect(&opn_stats) == 0) {
                gui_update_opnsense(&opn_stats);
                gui_update_wan_throughput(opn_stats.wan_in_bps, opn_stats.wan_out_bps);
            }
            last_stats_update = now;
        }

        display_render();
        nanosleep(&sleep_ts, NULL);
    }

    api_stop();
    backlight_off();
    if (has_touch) touch_cleanup();
    if (has_opnsense) opnsense_cleanup();
    gui_cleanup();
    display_close();

    return 0;
}
