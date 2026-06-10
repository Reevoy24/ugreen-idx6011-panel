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
#include "net_stats.h"
#include "disk_stats.h"
#include "pve_stats.h"
#include "gpu_stats.h"
#include "gui.h"
#include "config.h"
#include "settings.h"
#include "i18n.h"
#include "backlight.h"
#include "opnsense.h"
#include "touch.h"
#include "api.h"

static volatile int running = 1;
static volatile int signal_count = 0;

static ui_state_t ui_state;
static uint32_t bl_timeout_ms = 30000;

/* ---- settings panel actions ---- */
static void act_set_brightness(int pct) {
    backlight_set(pct);
    api_set_brightness(pct);
}

static void act_set_timeout(int seconds) {
    bl_timeout_ms = (uint32_t)seconds * 1000;
}

static void act_reboot(void) {
    fprintf(stderr, "Reboot requested from settings panel\n");
    backlight_off();
    /* systemctl on systemd distros, plain reboot on Unraid/others */
    if (system("systemctl reboot 2>/dev/null || reboot") != 0)
        fprintf(stderr, "Warning: reboot command failed\n");
}

static void act_poweroff(void) {
    fprintf(stderr, "Poweroff requested from settings panel\n");
    backlight_off();
    if (system("systemctl poweroff 2>/dev/null || poweroff") != 0)
        fprintf(stderr, "Warning: poweroff command failed\n");
}

static void signal_handler(int sig) {
    (void)sig;
    signal_count++;
    running = 0;
    if (signal_count >= 2) _exit(1);
}

// OS will display login screen on the display if bound, so we unbind first
// Works on Proxmox, Debian, and most Linux distros
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
    if (config.debug)
        fprintf(stderr, "Debug logging enabled\n");

    /* Free the touchscreen from i2c-hid before anything else; harmless if the
     * module is blacklisted or the device id differs (it just logs). */
    touch_unbind_i2c_hid(config.i2c_device);

    int dret = display_init(&config);
    if (dret != DISPLAY_OK) {
        if (dret == DISPLAY_NO_CONNECTOR) {
            /* Exit code 2 = unrecoverable on this boot; the service file uses
             * RestartPreventExitStatus=2 so systemd doesn't restart-loop. */
            fprintf(stderr, "Display init failed: no connected DRM connector (exit code 2)\n");
            return 2;
        }
        fprintf(stderr, "Display init failed\n");
        return 1;
    }

    /* panel-adjustable settings: state.json overrides config defaults */
    settings_load(&ui_state, config.brightness, config.backlight_timeout);
    i18n_set_language(ui_state.language);
    bl_timeout_ms = (uint32_t)ui_state.backlight_timeout * 1000;

    backlight_init();
    backlight_set(ui_state.brightness);
    api_set_brightness(ui_state.brightness);

    if (config.api_port > 0)
        api_start(config.api_port);

    int has_touch = (touch_init(config.touch_device) == 0);
    int has_opnsense = (opnsense_init(&config) == 0);
    if (has_opnsense)
        fprintf(stderr, "OPNsense API enabled: %s\n", config.opnsense_url);
    int has_gpu = (gpu_stats_init() == 0);

    /* The Proxmox page only exists when this host actually is a PVE node;
     * on TrueNAS/Unraid/plain Debian it is skipped entirely. */
    pve_stats_t pve_probe;
    pve_stats_collect(&pve_probe);
    int has_pve = pve_probe.available;
    fprintf(stderr, has_pve ? "Proxmox host detected — Proxmox page enabled\n"
                            : "No Proxmox host detected — Proxmox page disabled\n");

    gui_setup_t setup = {
        .show_opnsense = has_opnsense,
        .show_pve = has_pve,
        .wan_max_mbps = config.wan_max_mbps,
        .state = &ui_state,
        .set_brightness = act_set_brightness,
        .set_timeout = act_set_timeout,
        .do_reboot = act_reboot,
        .do_poweroff = act_poweroff,
    };
    gui_create_dashboard(&setup);

    if (has_touch)
        touch_lvgl_register();

    uint32_t last_stats_update = 0;
    uint32_t last_slow_update = 0;
    const uint32_t slow_interval = 10000; /* disks + pve every 10 s */
    uint32_t stats_interval = config.poll_rate * 1000;
    uint32_t last_touch_time = custom_tick_get();
    int screen_asleep = 0;
    system_stats_t stats;
    opnsense_stats_t opn_stats;
    struct timespec sleep_ts = { .tv_nsec = 33000000 };

    struct timespec sleep_long = { .tv_nsec = 100000000 }; /* 100ms poll while asleep */

    while (running) {
        uint32_t now = custom_tick_get();

        /* While awake the LVGL input device polls the touchscreen; while
         * asleep the loop below polls it directly. Either way the last
         * activity timestamp drives wake + idle timeout. */
        if (has_touch) {
            if (screen_asleep)
                touch_poll();
            uint32_t activity = touch_last_activity();
            if (activity > last_touch_time)
                last_touch_time = activity;
            if (screen_asleep && activity && now - activity < 500) {
                backlight_set(api_get_brightness());
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

            net_stats_t net;
            if (net_stats_collect(&net) == 0)
                gui_update_net(&net);

            if (has_gpu)
                gui_update_gpu(gpu_stats_usage());

            if (has_opnsense && opnsense_collect(&opn_stats) == 0) {
                gui_update_opnsense(&opn_stats);
                gui_update_wan_throughput(opn_stats.wan_in_bps, opn_stats.wan_out_bps);
            }

            if (now - last_slow_update >= slow_interval) {
                disk_stats_t disks;
                if (disk_stats_collect(&disks) == 0)
                    gui_update_disks(&disks);
                if (has_pve) {
                    pve_stats_t pve;
                    if (pve_stats_collect(&pve) == 0)
                        gui_update_pve(&pve);
                }
                last_slow_update = now;
            }
            last_stats_update = now;
        }

        display_render();
        nanosleep(&sleep_ts, NULL);
    }

    api_stop();
    gpu_stats_cleanup();
    backlight_off();
    if (has_touch) touch_cleanup();
    if (has_opnsense) opnsense_cleanup();
    gui_cleanup();
    display_close();

    return 0;
}
