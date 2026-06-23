#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

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

/* Guards ui_state across the GUI thread (settings callbacks) and the web-API
 * thread. Shared with gui.c via gui_setup_t.settings_lock. */
static pthread_mutex_t settings_lock = PTHREAD_MUTEX_INITIALIZER;

/* Latest fan-daemon status, kept for the web-API snapshot (read_fand_status
 * also feeds the on-device fan page). */
static int  g_fan_running = 0, g_fan_cpu = -1, g_fan_sys = -1;
static int  g_fan_cpu_pct = -1, g_fan_sys_pct = -1;
static long g_fan_rpm[4] = { -1, -1, -1, -1 };
static char g_fan_mode[16] = "", g_fan_cpu_curve[192] = "", g_fan_sys_curve[192] = "";

static api_snapshot_t g_snap; /* assembled each poll cycle, published to the API */

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

/* Set one key in ug-fand's config, preserving every other line. Atomic
 * (temp + rename, so the daemon's mtime hot-reload never sees a torn file) and
 * serialized so the panel (GUI thread) and the web API (API thread) can't
 * corrupt the file by writing at once. */
static int fand_config_set_key(const char *key, const char *value) {
    static pthread_mutex_t fand_lock = PTHREAD_MUTEX_INITIALIZER;
    const char *path = "/etc/ug-fand/config";
    const char *tmp  = "/etc/ug-fand/config.tmp";
    char lines[64][256];
    int n = 0, found = 0, rc = -1;
    size_t klen = strlen(key);

    pthread_mutex_lock(&fand_lock);
    FILE *f = fopen(path, "r");
    if (f) {
        while (n < 64 && fgets(lines[n], sizeof(lines[n]), f)) {
            if (strncmp(lines[n], key, klen) == 0 && lines[n][klen] == '=') {
                snprintf(lines[n], sizeof(lines[n]), "%s=%s\n", key, value);
                found = 1;
            }
            n++;
        }
        fclose(f);
    }
    if (!found && n < 64) {
        snprintf(lines[n], sizeof(lines[n]), "%s=%s\n", key, value);
        n++;
    }
    FILE *w = fopen(tmp, "w");
    if (w) {
        for (int i = 0; i < n; i++) fputs(lines[i], w);
        fclose(w);
        if (rename(tmp, path) == 0) rc = 0;
    }
    pthread_mutex_unlock(&fand_lock);
    if (rc) fprintf(stderr, "fan: cannot write %s\n", path);
    return rc;
}

/* Write ug-fand's mode (from the on-device fan page). */
static void act_set_fan_mode(const char *mode) { fand_config_set_key("mode", mode); }

/* Web-API: write any ug-fand config key (mode or a curve). */
int api_fand_set(const char *key, const char *value) { return fand_config_set_key(key, value); }

/* Web-API: power off / reboot (response already sent by the api.c handler). */
void api_action_power(int poweroff) {
    if (poweroff) act_poweroff();
    else          act_reboot();
}

/* Pull the fan daemon's live status into the fan page (NULL mode = not running)
 * and cache it for the web-API snapshot. */
static void read_fand_status(int update_gui) {
    int cpu_t = -1, sys_t = -1, cp = -1, sp = -1, running = 0;
    long rpm[4] = { -1, -1, -1, -1 };
    char mode[16] = "", cpu_curve[192] = "", sys_curve[192] = "";
    FILE *f = fopen("/run/ug-fand/status", "r");
    if (f) {
        running = 1;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if      (sscanf(line, "cpu_temp=%d", &cpu_t) == 1) continue;
            else if (sscanf(line, "sys_temp=%d", &sys_t) == 1) continue;
            else if (sscanf(line, "cpufan1=%ld", &rpm[0]) == 1) continue;
            else if (sscanf(line, "cpufan2=%ld", &rpm[1]) == 1) continue;
            else if (sscanf(line, "sysfan1=%ld", &rpm[2]) == 1) continue;
            else if (sscanf(line, "sysfan2=%ld", &rpm[3]) == 1) continue;
            else if (sscanf(line, "cpu_pct=%d", &cp) == 1) continue;
            else if (sscanf(line, "sys_pct=%d", &sp) == 1) continue;
            else if (sscanf(line, "cpu_curve=%191[^\n]", cpu_curve) == 1) continue;
            else if (sscanf(line, "sys_curve=%191[^\n]", sys_curve) == 1) continue;
            else    sscanf(line, "mode=%15s", mode);
        }
        fclose(f);
    }
    if (update_gui)
        gui_update_fans(cpu_t, sys_t, rpm, mode[0] ? mode : NULL, cpu_curve, sys_curve);

    g_fan_running = running && mode[0];
    g_fan_cpu = cpu_t; g_fan_sys = sys_t;
    g_fan_cpu_pct = cp; g_fan_sys_pct = sp;
    memcpy(g_fan_rpm, rpm, sizeof(g_fan_rpm));
    snprintf(g_fan_mode, sizeof(g_fan_mode), "%s", mode);
    snprintf(g_fan_cpu_curve, sizeof(g_fan_cpu_curve), "%s", cpu_curve);
    snprintf(g_fan_sys_curve, sizeof(g_fan_sys_curve), "%s", sys_curve);
}

/* Web-API: apply a partial settings update (already validated by api.c).
 * Inline-safe fields run here under settings_lock; LVGL-touching changes are
 * queued for the main loop. Returns 0. */
int api_apply_settings(const api_settings_patch_t *p) {
    int changed = 0, lang_changed = 0;

    pthread_mutex_lock(&settings_lock);
    if (p->has_brightness) { ui_state.brightness = p->brightness; act_set_brightness(p->brightness); changed = 1; }
    if (p->has_timeout)    { ui_state.backlight_timeout = p->timeout; act_set_timeout(p->timeout); changed = 1; }
    if (p->has_sleep)      { ui_state.sleep_brightness = p->sleep_brightness; changed = 1; }
    if (p->has_clock_24h)  { ui_state.clock_24h = p->clock_24h; changed = 1; }
    if (p->has_language)   { snprintf(ui_state.language, sizeof(ui_state.language), "%.3s", p->language);
                             i18n_set_language(p->language); lang_changed = 1; changed = 1; }
    if (changed) settings_save(&ui_state);
    pthread_mutex_unlock(&settings_lock);

    /* GUI-touching changes → main thread */
    if (lang_changed)     { api_cmd_t c = { .type = API_CMD_RETRANSLATE }; api_cmd_push(&c); }
    if (p->has_wallpaper) { api_cmd_t c = { .type = API_CMD_WALLPAPER_SET };
                            snprintf(c.arg_str, sizeof(c.arg_str), "%s", p->wallpaper); api_cmd_push(&c); }
    if (p->has_leds_on)   { api_cmd_t c = { .type = API_CMD_LEDS_TOGGLE,   .arg_int = p->leds_on };   api_cmd_push(&c); }
    if (p->has_led_night) { api_cmd_t c = { .type = API_CMD_LEDS_SET_NIGHT, .arg_int = p->led_night }; api_cmd_push(&c); }
    if (p->has_night_start && p->has_night_end) {
        api_cmd_t c = { .type = API_CMD_SET_NIGHT_WINDOW };
        snprintf(c.arg_str, sizeof(c.arg_str), "%s-%s", p->night_start, p->night_end);
        api_cmd_push(&c);
    }
    if (p->has_timezone) {
        api_cmd_t c = { .type = API_CMD_SET_TIMEZONE };
        snprintf(c.arg_str, sizeof(c.arg_str), "%s", p->timezone);
        api_cmd_push(&c);
    }
    return 0;
}

/* Assemble + publish the web-API snapshot from the latest collected stats. */
static void publish_snapshot(const system_stats_t *sys, const net_stats_t *net,
                             const disk_stats_t *disks, const pve_stats_t *pve,
                             const opnsense_stats_t *opn, float gpu,
                             int has_gpu, int has_pve, int has_opn,
                             int has_leds, int has_touch) {
    memset(&g_snap, 0, sizeof(g_snap));
    g_snap.valid = 1;
    g_snap.sys = *sys;
    g_snap.has_gpu = has_gpu; g_snap.gpu_usage = gpu;
    g_snap.net = *net;
    g_snap.disks = *disks;
    g_snap.has_pve = has_pve; if (pve) g_snap.pve = *pve;
    g_snap.has_opnsense = has_opn; if (opn) g_snap.opn = *opn;
    g_snap.has_leds = has_leds; g_snap.has_touch = has_touch;

    g_snap.fan_running = g_fan_running;
    g_snap.fan_cpu_temp = g_fan_cpu; g_snap.fan_sys_temp = g_fan_sys;
    g_snap.fan_cpu_pct = g_fan_cpu_pct; g_snap.fan_sys_pct = g_fan_sys_pct;
    memcpy(g_snap.fan_rpm, g_fan_rpm, sizeof(g_snap.fan_rpm));
    snprintf(g_snap.fan_mode, sizeof(g_snap.fan_mode), "%s", g_fan_mode);
    snprintf(g_snap.fan_cpu_curve, sizeof(g_snap.fan_cpu_curve), "%s", g_fan_cpu_curve);
    snprintf(g_snap.fan_sys_curve, sizeof(g_snap.fan_sys_curve), "%s", g_fan_sys_curve);

    pthread_mutex_lock(&settings_lock);
    g_snap.brightness = ui_state.brightness;
    g_snap.backlight_timeout = ui_state.backlight_timeout;
    g_snap.sleep_brightness = ui_state.sleep_brightness;
    g_snap.leds_on = ui_state.leds_on;
    g_snap.led_night = ui_state.led_night;
    g_snap.clock_24h = ui_state.clock_24h;
    snprintf(g_snap.language, sizeof(g_snap.language), "%s", ui_state.language);
    snprintf(g_snap.wallpaper, sizeof(g_snap.wallpaper), "%s", ui_state.wallpaper);
    snprintf(g_snap.led_night_start, sizeof(g_snap.led_night_start), "%s", ui_state.led_night_start);
    snprintf(g_snap.led_night_end, sizeof(g_snap.led_night_end), "%s", ui_state.led_night_end);
    snprintf(g_snap.timezone, sizeof(g_snap.timezone), "%s", ui_state.timezone);
    pthread_mutex_unlock(&settings_lock);

    if (has_leds)
        snprintf(g_snap.led_night_window, sizeof(g_snap.led_night_window), "%s", leds_night_window());
    g_snap.wp_count = gui_wallpaper_options(g_snap.wp_opts, API_WP_MAX, &g_snap.wp_cur);

    api_publish_stats(&g_snap);
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

/* System uptime in seconds (0 on error). Used to detect a cold boot: when the
 * daemon starts at low uptime the panel/ITE EC may not be ready to accept the
 * backlight command for a while, so the display init succeeds but the panel
 * stays dark. On a manual `systemctl restart` the uptime is already high, so
 * the cold-boot settle below is skipped. */
static double system_uptime(void) {
    double up = 0.0;
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        if (fscanf(f, "%lf", &up) != 1) up = 0.0;
        fclose(f);
    }
    return up;
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
    touch_set_debug(config.debug);
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
    settings_load(&ui_state, &config);
    i18n_set_language(ui_state.language);
    bl_timeout_ms = (uint32_t)ui_state.backlight_timeout * 1000;
    if (ui_state.timezone[0]) { setenv("TZ", ui_state.timezone, 1); tzset(); }

    backlight_init();
    backlight_set(ui_state.brightness);
    api_set_brightness(ui_state.brightness);

    if (config.api_port > 0)
        api_start(config.api_port, config.api_password);

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
    int has_leds = leds_init(ui_state.led_night_start, ui_state.led_night_end);
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
        .settings_lock = &settings_lock,
        .set_brightness = act_set_brightness,
        .set_timeout = act_set_timeout,
        .do_reboot = act_reboot,
        .do_poweroff = act_poweroff,
        .set_fan_mode = act_set_fan_mode,
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
    double boot_uptime = system_uptime(); /* for the cold-boot backlight settle */
    uint32_t loop_start = custom_tick_get();
    uint32_t last_settle = 0;
    int ec_ready = 0;              /* set once the EC accepts a backlight write */
    system_stats_t stats;
    opnsense_stats_t opn_stats;
    /* persistent across cycles so the web-API snapshot keeps the last values
     * (disks/pve refresh only every 10 s). */
    net_stats_t net;     memset(&net, 0, sizeof(net));
    disk_stats_t disks;  memset(&disks, 0, sizeof(disks));
    pve_stats_t pve;     memset(&pve, 0, sizeof(pve));
    float gpu_usage = -1.0f;
    struct timespec sleep_ts = { .tv_nsec = 33000000 };

    /* While a swipe/panel animation runs (or a finger is on the glass) the
     * loop spins much faster so LVGL can hit its refresh period — with the
     * fixed 33 ms sleep, transitions involving the render-heavy home page
     * dropped to ~10 fps and visibly lagged. */
    struct timespec sleep_anim = { .tv_nsec = 4000000 }; /* 4ms while animating */

    struct timespec sleep_long = { .tv_nsec = 50000000 }; /* 50ms touch poll while asleep */

    time_t last_led_check = 0;

    while (running) {
        /* Drain queued web-API actions and run them here on the GUI thread
         * (the API thread never touches LVGL). Runs even while asleep, so a
         * wallpaper/language change applies and shows on the next wake. */
        api_cmd_t cmd;
        while (api_cmd_pop(&cmd)) {
            switch (cmd.type) {
            case API_CMD_RETRANSLATE:
                gui_retranslate();
                break;
            case API_CMD_WALLPAPER_SET:
                pthread_mutex_lock(&settings_lock);
                snprintf(ui_state.wallpaper, sizeof(ui_state.wallpaper), "%.31s", cmd.arg_str);
                gui_wallpaper_set(cmd.arg_str);
                settings_save(&ui_state);
                pthread_mutex_unlock(&settings_lock);
                break;
            case API_CMD_WALLPAPER_RESCAN:
                pthread_mutex_lock(&settings_lock);
                snprintf(ui_state.wallpaper, sizeof(ui_state.wallpaper), "custom");
                gui_wallpaper_rescan();
                settings_save(&ui_state);
                pthread_mutex_unlock(&settings_lock);
                break;
            case API_CMD_WALLPAPER_DELETE:
                /* custom file already removed; drop "custom" and fall back if it
                 * was selected, then re-scan + re-apply. */
                pthread_mutex_lock(&settings_lock);
                if (strcmp(ui_state.wallpaper, "custom") == 0)
                    snprintf(ui_state.wallpaper, sizeof(ui_state.wallpaper), "none");
                gui_wallpaper_rescan();
                settings_save(&ui_state);
                pthread_mutex_unlock(&settings_lock);
                break;
            case API_CMD_LEDS_TOGGLE:
                if (cmd.arg_int != leds_user_on()) leds_toggle();
                pthread_mutex_lock(&settings_lock);
                ui_state.leds_on = leds_user_on();
                settings_save(&ui_state);
                pthread_mutex_unlock(&settings_lock);
                gui_leds_refresh();
                break;
            case API_CMD_LEDS_SET_NIGHT:
                leds_set_night(cmd.arg_int);
                pthread_mutex_lock(&settings_lock);
                ui_state.led_night = leds_night_enabled();
                settings_save(&ui_state);
                pthread_mutex_unlock(&settings_lock);
                gui_leds_refresh();
                break;
            case API_CMD_SET_NIGHT_WINDOW: {
                char ns[8] = "", ne[8] = "";
                sscanf(cmd.arg_str, "%7[^-]-%7s", ns, ne);
                leds_set_window(ns, ne);
                pthread_mutex_lock(&settings_lock);
                snprintf(ui_state.led_night_start, sizeof(ui_state.led_night_start), "%s", ns);
                snprintf(ui_state.led_night_end, sizeof(ui_state.led_night_end), "%s", ne);
                settings_save(&ui_state);
                pthread_mutex_unlock(&settings_lock);
                gui_leds_refresh();
                break;
            }
            case API_CMD_SET_TIMEZONE:
                if (cmd.arg_str[0]) setenv("TZ", cmd.arg_str, 1);
                else unsetenv("TZ");
                tzset();
                pthread_mutex_lock(&settings_lock);
                snprintf(ui_state.timezone, sizeof(ui_state.timezone), "%.39s", cmd.arg_str);
                settings_save(&ui_state);
                pthread_mutex_unlock(&settings_lock);
                break;
            }
        }

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
            if (touched || api_get_state()) {
                SLEEPLOG("wake (%s)", touched ? "touch" : "api");
                gui_set_sleep(0);
                backlight_set(api_get_brightness());
                screen_asleep = 0;
                api_set_state(1);
                last_touch_time = custom_tick_get();
            } else {
                /* Keep the web dashboard live while the screen is off: refresh
                 * stats at the normal cadence (sensor reads + publish only — no
                 * GUI, no EC/backlight). Gated on the web API being enabled so a
                 * panel without a dashboard still sleeps fully idle. */
                if (config.api_port > 0) {
                    uint32_t nowz = custom_tick_get();
                    if (nowz - last_stats_update >= stats_interval) {
                        system_stats_collect(&stats);
                        net_stats_collect(&net);
                        if (has_gpu) gpu_usage = gpu_stats_usage();
                        read_fand_status(0);
                        if (has_opnsense) opnsense_collect(&opn_stats);
                        if (nowz - last_slow_update >= slow_interval) {
                            disk_stats_collect(&disks);
                            if (has_pve) pve_stats_collect(&pve);
                            last_slow_update = nowz;
                        }
                        publish_snapshot(&stats, &net, &disks, &pve,
                                         has_opnsense ? &opn_stats : NULL, gpu_usage,
                                         has_gpu, has_pve, has_opnsense, has_leds, has_touch);
                        last_stats_update = nowz;
                    }
                }
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

        /* Cold-boot backlight settle: on a cold boot the panel/ITE EC may not
         * accept the backlight command immediately — display init succeeds but
         * the panel can stay dark. While settling we re-assert the backlight
         * every 3 s and hold off sleeping, so the screen lights up the moment
         * the EC is ready. Settling ENDS as soon as the EC accepts a write
         * (so the normal idle timeout takes over right away — not after a fixed
         * window), or at boot_settle_secs as a hard cap. A warm manual restart
         * starts past the cap, so this is inactive then. */
        double cur_uptime = boot_uptime + (double)(now - loop_start) / 1000.0;
        int warming = !ec_ready && config.boot_settle_secs > 0 &&
                      cur_uptime < (double)config.boot_settle_secs;
        if (warming && !screen_asleep && (int32_t)(now - last_settle) >= 3000) {
            last_settle = now;
            if (backlight_set_checked(api_get_brightness()) == 0) {
                ec_ready = 1; /* panel lit → stop settling, normal timeout resumes */
                last_touch_time = now; /* count the idle timeout from "screen up" */
                SLEEPLOG("backlight up at uptime=%.0fs — idle timeout active", cur_uptime);
            }
            lv_obj_invalidate(lv_screen_active());
        }

        /* Idle timeout: the screen turns off after the configured idle time,
         * counted from the last touch (or from when the panel first lit at
         * boot). A physical touch — or the HTTP API — wakes it; until then it
         * stays asleep, which is the whole point of a timeout. The touch chip
         * reliably produces a valid frame on contact, so wake works. */
        int idle_hit = has_touch && bl_timeout_ms > 0 &&
                       (int32_t)(now - last_touch_time) >= (int32_t)bl_timeout_ms;
        if (!warming && (idle_hit || !api_get_state())) {
            gui_set_sleep(1);
            lv_refr_now(NULL); /* paint the black frame before pausing renders */
            if (ui_state.sleep_brightness <= 0)
                backlight_off();
            else
                backlight_set(ui_state.sleep_brightness);
            screen_asleep = 1;
            api_set_state(0);
            SLEEPLOG("sleep (backlight %s)", ui_state.sleep_brightness <= 0 ? "off" : "dim");
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

            if (net_stats_collect(&net) == 0)
                gui_update_net(&net);

            if (has_gpu) {
                gpu_usage = gpu_stats_usage();
                gui_update_gpu(gpu_usage);
            }

            read_fand_status(1);

            if (has_opnsense && opnsense_collect(&opn_stats) == 0) {
                gui_update_opnsense(&opn_stats);
                gui_update_wan_throughput(opn_stats.wan_in_bps, opn_stats.wan_out_bps);
            }

            if (now - last_slow_update >= slow_interval) {
                if (disk_stats_collect(&disks) == 0)
                    gui_update_disks(&disks);
                if (has_pve && pve_stats_collect(&pve) == 0)
                    gui_update_pve(&pve);
                last_slow_update = now;
            }

            publish_snapshot(&stats, &net, &disks, &pve,
                             has_opnsense ? &opn_stats : NULL, gpu_usage,
                             has_gpu, has_pve, has_opnsense, has_leds, has_touch);
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
