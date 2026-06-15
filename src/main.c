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
#include "leds.h"

static volatile int running = 1;
static volatile int signal_count = 0;

static ui_state_t ui_state;
static uint32_t bl_timeout_ms = 30000;

/* Transition logging, always on (transitions are infrequent — no journal spam).
 * This is the data we lacked when v1.4.2 bricked the display. */
#define SLEEPLOG(...) do { fprintf(stderr, "ug-paneld[disp]: " __VA_ARGS__); fputc('\n', stderr); } while (0)

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

    /* Front LED rows appear only when the LED setup exists on this host
     * (led-ugreen kernel module or ugreen_leds_cli; tools/setup-ugreen-leds.sh). */
    int has_leds = leds_init(config.led_night_start, config.led_night_end);
    if (has_leds) {
        leds_startup(ui_state.leds_on, ui_state.led_night);
        fprintf(stderr, "Front LED control enabled (night window %s)\n",
                leds_night_window());
    } else {
        fprintf(stderr, "No front LED control found — LED settings hidden\n");
    }

    gui_setup_t setup = {
        .show_opnsense = has_opnsense,
        .show_pve = has_pve,
        .show_leds = has_leds,
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
    uint32_t sleep_entered_at = 0; /* for the safety watchdog */
    int sleep_disabled = 0;        /* set if the watchdog had to rescue a dark screen */
    uint32_t boot_ms = custom_tick_get(); /* for the cold-boot backlight settle */
    uint32_t last_settle = 0;
    system_stats_t stats;
    opnsense_stats_t opn_stats;
    struct timespec sleep_ts = { .tv_nsec = 33000000 };

    /* While a swipe/panel animation runs (or a finger is on the glass) the
     * loop spins much faster so LVGL can hit its refresh period — with the
     * fixed 33 ms sleep, transitions involving the render-heavy home page
     * dropped to ~10 fps and visibly lagged. */
    struct timespec sleep_anim = { .tv_nsec = 4000000 }; /* 4ms while animating */

    struct timespec sleep_long = { .tv_nsec = 50000000 }; /* 50ms touch poll while asleep */

    time_t last_led_check = 0;

    while (running) {
        /* LED night window — checked even while the screen sleeps (that is
         * exactly when the 21:00 transition usually happens). */
        if (has_leds) {
            time_t tnow = time(NULL);
            if (tnow != last_led_check) {
                last_led_check = tnow;
                if (leds_tick(tnow))
                    gui_leds_refresh();
            }
        }

        /* While awake the LVGL input device polls the touchscreen; while
         * asleep the loop polls it directly and wakes on the poll result. */
        if (screen_asleep) {
            int touched = has_touch ? touch_poll() : 0;
            /* Safety net for the COLD case only: until a real touch has ever
             * woken the screen we cannot be sure a cold sleep is wakeable, so
             * if nothing wakes it within a few minutes, force it back on and
             * stop auto-sleeping until a tap proves wake works. Once any touch
             * has woken it, this is disabled (no spurious wakes on an idle but
             * working unit). With working touch this never fires. */
            uint32_t wnow = custom_tick_get();
            uint32_t wd_ms = bl_timeout_ms * 3u;
            if (wd_ms < 180000u) wd_ms = 180000u; /* never below 3 min */
            int watchdog = (touch_last_activity() == 0) &&
                           (int32_t)(wnow - sleep_entered_at) >= (int32_t)wd_ms;

            if (touched || api_get_state() || watchdog) {
                if (watchdog && !touched && !api_get_state()) {
                    SLEEPLOG("watchdog force-wake after %u ms — auto-sleep off until next tap",
                             (unsigned)(wnow - sleep_entered_at));
                    sleep_disabled = 1;
                } else {
                    SLEEPLOG("wake (%s)", touched ? "touch" : "api");
                    if (touched) sleep_disabled = 0;
                }
                gui_set_sleep(0);
                backlight_set(api_get_brightness());
                screen_asleep = 0;
                api_set_state(1);
                last_touch_time = custom_tick_get();
            } else {
                nanosleep(&sleep_long, NULL);
                continue;
            }
        }

        if (has_touch) {
            uint32_t activity = touch_last_activity();
            if (activity > last_touch_time)
                last_touch_time = activity;
        }

        /* Sampled after the wake/poll section so it is never older than the
         * touch timestamps (an earlier sample caused an unsigned underflow
         * in the idle check and a wake/sleep oscillation). */
        uint32_t now = custom_tick_get();

        /* Cold-boot backlight settle: at early boot the ITE EC may not accept
         * the backlight command yet — the panel powers up but stays dark until
         * a later restart, even though DRM/render are fine. Re-assert the
         * backlight and force a redraw every 2 s for the first ~20 s so the
         * screen lights up the moment the EC is ready, no manual restart. */
        if (!screen_asleep && (int32_t)(now - boot_ms) < 20000 &&
            (int32_t)(now - last_settle) >= 2000) {
            last_settle = now;
            backlight_set(api_get_brightness());
            lv_obj_invalidate(lv_screen_active());
        }

        /* Idle timeout counts from boot: the screen turns itself off after the
         * configured time even if never touched. The touch chip emits constant
         * garbage (0x22) whenever it is NOT physically touched and only returns
         * a valid frame DURING a touch — so wake capability cannot be verified
         * from idle reads. We sleep on timeout and let a physical touch wake it
         * (the chip does produce a valid frame on contact); the cold watchdog
         * above guarantees recovery if a pre-first-touch cold wake ever fails. */
        int idle_hit = has_touch && bl_timeout_ms > 0 && !sleep_disabled &&
                       (int32_t)(now - last_touch_time) >= (int32_t)bl_timeout_ms;
        if (idle_hit || !api_get_state()) {
            gui_set_sleep(1);
            lv_refr_now(NULL); /* paint the black frame before pausing renders */
            if (config.sleep_brightness <= 0)
                backlight_off();
            else
                backlight_set(config.sleep_brightness);
            screen_asleep = 1;
            sleep_entered_at = now;
            api_set_state(0);
            SLEEPLOG("sleep (backlight %s)", config.sleep_brightness <= 0 ? "off" : "dim");
            nanosleep(&sleep_long, NULL);
            continue;
        }

        gui_update_clock();

        int animating = lv_anim_count_running() > 0 ||
                        (has_touch && now - touch_last_activity() < 150);

        /* Defer stats refreshes while a transition is on screen: chart and
         * label invalidations mid-swipe enlarge the redraw area and cause
         * visible hitches. The data catches up a few frames later. */
        if (now - last_stats_update >= stats_interval && !animating) {
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
        nanosleep(animating ? &sleep_anim : &sleep_ts, NULL);
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
