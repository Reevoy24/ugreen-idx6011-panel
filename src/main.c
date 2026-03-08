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

static volatile int running = 1;
static volatile int signal_count = 0;

static void signal_handler(int sig) {
    (void)sig;
    signal_count++;
    running = 0;
    if (signal_count >= 2) _exit(1);
}

/*
This kills the services that are present on UGOS
Not needed on other systems
static void kill_existing(void) {
    system("killall mini_screen 2>/dev/null");
    system("killall plymouthd 2>/dev/null");
}*/

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

    //kill_existing();
    unbind_vt_console();

    config_t config;
    if (config_load(&config) != 0)
        fprintf(stderr, "Warning: Failed to load config, using defaults\n");

    int width, height;
    if (display_init(&width, &height) != 0) {
        fprintf(stderr, "Display init failed\n");
        return 1;
    }

    backlight_on();

    lv_obj_t *screen = gui_create_dashboard();

    uint32_t last_stats_update = 0;
    uint32_t stats_interval = config.refresh_rate * 1000;
    system_stats_t stats;
    struct timespec sleep_ts = { .tv_nsec = 33000000 };

    while (running) {
        uint32_t now = custom_tick_get();

        gui_update_clock();

        if (now - last_stats_update >= stats_interval) {
            if (system_stats_collect(&stats) == 0)
                gui_update_dashboard(screen, &stats);
            last_stats_update = now;
        }

        display_render();
        nanosleep(&sleep_ts, NULL);
    }

    backlight_off();
    gui_cleanup();
    display_close();

    return 0;
}
