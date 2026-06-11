#include "leds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CLI_PATH      "/usr/local/bin/ugreen_leds_cli"
#define STATIC_SCRIPT "/usr/local/bin/ugreen-leds-static"

enum { BACKEND_NONE, BACKEND_SYSFS, BACKEND_CLI };

static int backend = BACKEND_NONE;
static int user_on = 1;          /* persisted master switch */
static int night_on = 0;         /* persisted night mode */
static int night_override = 0;   /* "on anyway" until the current window ends */
static int night_active_now = 0;
static int applied_state = -1;   /* last state written to the hardware */
static int start_min = 21 * 60;
static int end_min = 8 * 60;
/* sized for the worst-case "%02d" expansion so -Wformat-truncation is happy */
static char window_str[48] = "21:00-08:00";

static const char *sysfs_leds[] = {
    "power", "network_stat", "network_stat2",
    "disk1", "disk2", "disk3", "disk4", "disk5", "disk6",
};
#define SYSFS_LED_COUNT (sizeof(sysfs_leds) / sizeof(sysfs_leds[0]))

static int path_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static int parse_hhmm(const char *s, int fallback)
{
    int h = 0, m = 0;
    if (!s || sscanf(s, "%d:%d", &h, &m) < 1) return fallback;
    if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
    return h * 60 + m;
}

static int detect_backend(void)
{
    if (path_exists("/sys/class/leds/power/brightness") &&
        path_exists("/sys/class/leds/disk1/brightness"))
        return BACKEND_SYSFS;
    /* Module loaded but LEDs not registered yet (probe still pending at
     * boot): claim sysfs anyway — never race the kernel driver with the
     * CLI. The writes are no-ops until the LEDs appear. */
    if (path_exists("/sys/module/led_ugreen"))
        return BACKEND_SYSFS;
    if (access(CLI_PATH, X_OK) == 0)
        return BACKEND_CLI;
    return BACKEND_NONE;
}

static void sysfs_write(const char *led, const char *attr, const char *val)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/%s", led, attr);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(val, f);
    fclose(f);
}

static int sysfs_read_int(const char *led, const char *attr)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/leds/%s/%s", led, attr);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static void run(const char *cmd)
{
    if (system(cmd) != 0)
        fprintf(stderr, "leds: command failed: %s\n", cmd);
}

static void apply(int on)
{
    if (backend == BACKEND_NONE || applied_state == on) return;
    applied_state = on;
    fprintf(stderr, "leds: %s (backend: %s)\n", on ? "on" : "off",
            backend == BACKEND_SYSFS ? "sysfs" : "cli");

    if (backend == BACKEND_SYSFS) {
        if (on) {
            /* The monitors restore triggers, colors and brightness.
             * diskiomon Requires= the probe unit, so ordering is safe. */
            run("systemctl start ugreen-diskiomon.service 2>/dev/null; "
                "systemctl restart ugreen-idx-netled.service 2>/dev/null");
            /* Belt and braces: light the power LED directly too, so "on"
             * is visible (and the drift check stable) even when the
             * ugreen-idx-netled unit is missing on this install. */
            sysfs_write("power", "trigger", "none");
            sysfs_write("power", "color", "255 255 255");
            sysfs_write("power", "brightness", "160");
        } else {
            /* Stop the disk monitor first so nothing re-lights the LEDs,
             * then kill triggers (netdev keeps blinking otherwise). */
            run("systemctl stop ugreen-diskiomon.service 2>/dev/null");
            for (size_t i = 0; i < SYSFS_LED_COUNT; i++) {
                sysfs_write(sysfs_leds[i], "trigger", "none");
                sysfs_write(sysfs_leds[i], "brightness", "0");
            }
        }
    } else { /* BACKEND_CLI */
        if (on) {
            if (path_exists(STATIC_SCRIPT))
                run(STATIC_SCRIPT " >/dev/null 2>&1");
            else
                run("UGREEN_MODEL=idx6011 " CLI_PATH
                    " all -on >/dev/null 2>&1");
        } else {
            run("UGREEN_MODEL=idx6011 " CLI_PATH
                " all -off >/dev/null 2>&1");
        }
    }
}

static int in_window(const struct tm *tm)
{
    if (start_min == end_min) return 0;
    int t = tm->tm_hour * 60 + tm->tm_min;
    if (start_min < end_min)
        return t >= start_min && t < end_min;
    return t >= start_min || t < end_min; /* window crosses midnight */
}

static int effective(void)
{
    return user_on && !(night_on && night_active_now && !night_override);
}

int leds_init(const char *night_start, const char *night_end)
{
    start_min = parse_hhmm(night_start, 21 * 60);
    end_min = parse_hhmm(night_end, 8 * 60);
    snprintf(window_str, sizeof(window_str), "%02d:%02d-%02d:%02d",
             start_min / 60, start_min % 60, end_min / 60, end_min % 60);
    backend = detect_backend();
    return backend != BACKEND_NONE;
}

int leds_available(void)
{
    return backend != BACKEND_NONE;
}

void leds_startup(int on, int night_enabled)
{
    user_on = !!on;
    night_on = !!night_enabled;
    night_override = 0;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    night_active_now = in_window(&tm);
    apply(effective());
}

int leds_user_on(void)       { return user_on; }
int leds_night_enabled(void) { return night_on; }
int leds_effective_on(void)  { return effective(); }
const char *leds_night_window(void) { return window_str; }

void leds_toggle(void)
{
    if (effective()) {
        /* turning off: an active override just ends; otherwise it is intent */
        if (night_on && night_active_now && night_override)
            night_override = 0;
        else
            user_on = 0;
    } else {
        user_on = 1;
        if (night_on && night_active_now)
            night_override = 1; /* stay on until the window ends */
    }
    apply(effective());
}

void leds_set_night(int enabled)
{
    night_on = !!enabled;
    night_override = 0;
    apply(effective());
}

int leds_tick(time_t now)
{
    static time_t last = 0;
    if (now == last) return 0;
    last = now;

    /* Late backend: the kernel module / CLI can appear after daemon start
     * (boot ordering, or the user just ran setup-ugreen-leds.sh). */
    if (backend != BACKEND_SYSFS) {
        int nb = detect_backend();
        if (nb != backend && nb != BACKEND_NONE) {
            backend = nb;
            applied_state = -1; /* force re-apply through the new backend */
        }
    }
    if (backend == BACKEND_NONE) return 0;

    /* Drift check: at boot the LED services can start after the daemon and
     * re-light everything although the remembered state is "off" (and the
     * pre-registration sysfs writes were no-ops). The power LED brightness
     * mirrors our on/off writes, so a mismatch means someone else won. */
    if (backend == BACKEND_SYSFS && applied_state >= 0) {
        int hw = sysfs_read_int("power", "brightness");
        if (hw >= 0 && (hw > 0) != applied_state)
            applied_state = -1; /* force re-apply below */
    }

    struct tm tm;
    localtime_r(&now, &tm);
    int active = in_window(&tm);
    if (active != night_active_now) {
        night_active_now = active;
        if (!active) night_override = 0; /* an override lasts one window */
    }

    int want = effective();
    if (want != applied_state) {
        apply(want);
        return 1;
    }
    return 0;
}
