/*
 * ug-fand — fan monitor + control daemon for the UGREEN iDX6011 Pro.
 *
 * Talks to the ITE IT55xx embedded controller as a standard ACPI EC
 * (ports 0x62 data / 0x66 cmd+status) — the SAME EC the panel uses for the
 * backlight. Reverse-engineered from UGOS' ug_idx6011pro-sio.ko:
 *
 *   Fan tach (RPM, read, 16-bit big-endian):
 *     cpufan1 EC[0x34:0x35]  cpufan2 EC[0x36:0x37]
 *     sysfan1 EC[0x38:0x39]  sysfan2 EC[0x3A:0x3B]
 *   Fan duty (write; per fan: enable byte = 1, then duty 0..198):
 *     cpufan1 EC[0xB0]=1,[0xB1]=duty  cpufan2 EC[0xB2]=1,[0xB3]=duty
 *     sysfan1 EC[0xB4]=1,[0xB5]=duty  sysfan2 EC[0xB6]=1,[0xB7]=duty
 *
 * Runs on Proxmox / TrueNAS / Debian (anywhere with /dev port I/O as root).
 * NOT for UGOS — there the proprietary driver owns the EC and would fight us.
 *
 * Config (/etc/ug-fand/config): mode, interval, and per-mode fan curves
 * (temp:duty point lists). Needs root (port I/O via ioperm).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/io.h>
#include <sys/file.h>
#include <sys/stat.h>

/* ---- EC interface ---- */
#define EC_SC   0x66          /* command / status port */
#define EC_DATA 0x62          /* data port             */
#define EC_IBF  0x02          /* input buffer full     */
#define EC_OBF  0x01          /* output buffer full    */
#define EC_CMD_READ  0x80     /* read EC memory byte   */
#define EC_CMD_WRITE 0x81     /* write EC memory byte  */

/* Fan registers */
#define REG_CPUFAN1_RPM 0x34  /* hi,lo at 0x34/0x35 */
#define REG_CPUFAN2_RPM 0x36
#define REG_SYSFAN1_RPM 0x38
#define REG_SYSFAN2_RPM 0x3A
#define REG_CPU_EN1  0xB0     /* enable=1, duty, enable=1, duty */
#define REG_SYS_EN1  0xB4
#define DUTY_MAX 198          /* 0xC6 — EC clamps here */
#define DUTY_DEADBAND 6       /* hold steady for sub-deadband changes (anti-hunt) */

/* Critical temperatures (deg C) that force fans to full regardless of mode */
#define CPU_CRIT 88
#define SYS_CRIT 60

/* Shared with ug-paneld's backlight: serialize EC transactions */
#define EC_LOCK_PATH   "/run/ug-ec.lock"
#define CONFIG_PATH    "/etc/ug-fand/config"
#define STATUS_DIR     "/run/ug-fand"
#define STATUS_PATH    "/run/ug-fand/status"

static volatile sig_atomic_t running = 1;
static int lock_fd = -1;

/* ---- low level EC ---- */
static void ec_lock(void)   { if (lock_fd >= 0) flock(lock_fd, LOCK_EX); }
static void ec_unlock(void) { if (lock_fd >= 0) flock(lock_fd, LOCK_UN); }

static int ec_wait(int mask, int want) {
    for (int i = 0; i < 10000; i++) {
        int s = inb(EC_SC);
        if (((s & mask) != 0) == (want != 0)) return 0;
        struct timespec ts = { .tv_nsec = 50000 }; /* 50us */
        nanosleep(&ts, NULL);
    }
    return -1;
}

static int ec_read(uint8_t addr, uint8_t *out) {
    int rc = -1;
    ec_lock();
    if (ec_wait(EC_IBF, 0)) goto done;
    outb(EC_CMD_READ, EC_SC);
    if (ec_wait(EC_IBF, 0)) goto done;
    outb(addr, EC_DATA);
    if (ec_wait(EC_OBF, 1)) goto done;
    *out = inb(EC_DATA);
    rc = 0;
done:
    ec_unlock();
    return rc;
}

static int ec_write(uint8_t addr, uint8_t val) {
    int rc = -1;
    ec_lock();
    if (ec_wait(EC_IBF, 0)) goto done;
    outb(EC_CMD_WRITE, EC_SC);
    if (ec_wait(EC_IBF, 0)) goto done;
    outb(addr, EC_DATA);
    if (ec_wait(EC_IBF, 0)) goto done;
    outb(val, EC_DATA);
    rc = 0;
done:
    ec_unlock();
    return rc;
}

static long fan_rpm(uint8_t reg) {
    uint8_t hi, lo;
    if (ec_read(reg, &hi) || ec_read(reg + 1, &lo)) return -1;
    return ((long)hi << 8) | lo;
}

static void set_fan_pair(uint8_t base, int duty) {
    if (duty < 0) duty = 0;
    if (duty > DUTY_MAX) duty = DUTY_MAX;
    ec_write(base,     1);
    ec_write(base + 1, (uint8_t)duty);
    ec_write(base + 2, 1);
    ec_write(base + 3, (uint8_t)duty);
}

/* ---- temperatures (hwmon) ---- */
static int read_sysfs_int(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v = -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return (int)v;
}

static int hwmon_temp_max(const char *want) {
    int best = -1;
    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "hwmon", 5) != 0) continue;
        char p[512], name[64] = "";
        snprintf(p, sizeof(p), "/sys/class/hwmon/%s/name", e->d_name);
        FILE *nf = fopen(p, "r");
        if (nf) { if (fscanf(nf, "%63s", name) != 1) name[0] = 0; fclose(nf); }
        if (!strstr(name, want)) continue;
        for (int i = 1; i <= 24; i++) {
            snprintf(p, sizeof(p), "/sys/class/hwmon/%s/temp%d_input", e->d_name, i);
            int mC = read_sysfs_int(p);
            if (mC > 0 && mC / 1000 > best) best = mC / 1000;
        }
    }
    closedir(d);
    return best;
}

static int cpu_temp(void) {
    int t = hwmon_temp_max("coretemp");
    if (t < 0) t = hwmon_temp_max("k10temp");   /* AMD */
    if (t < 0) t = hwmon_temp_max("acpitz");
    return t;
}
static int sys_temp(void) {
    int t = hwmon_temp_max("nvme");
    int d = hwmon_temp_max("drivetemp");
    if (d > t) t = d;
    if (t < 0) t = hwmon_temp_max("acpitz");
    return t;
}

/* ---- fan curves ---- */
#define CURVE_MAX 12
typedef struct { int temp; int duty; } point_t;
typedef struct { point_t pts[CURVE_MAX]; int n; } curve_t;

typedef enum { MODE_SILENT, MODE_DEFAULT, MODE_TURBO, MODE_COUNT } fan_mode_t;
static const char *mode_name(fan_mode_t m) {
    return m == MODE_SILENT ? "silent" : m == MODE_TURBO ? "turbo" : "default";
}

typedef struct {
    fan_mode_t mode;
    int interval;
    int force;
    curve_t cpu[MODE_COUNT];
    curve_t sys[MODE_COUNT];
} fanconf_t;

/* Linear interpolation; table is sorted ascending by temp. */
static int curve_duty(const curve_t *cv, int temp) {
    const point_t *c = cv->pts; int n = cv->n;
    if (n <= 0) return DUTY_MAX;
    if (temp <= c[0].temp) return c[0].duty;
    if (temp >= c[n-1].temp) return c[n-1].duty;
    for (int i = 1; i < n; i++) {
        if (temp <= c[i].temp) {
            int t0 = c[i-1].temp, t1 = c[i].temp, d0 = c[i-1].duty, d1 = c[i].duty;
            return t1 == t0 ? d1 : d0 + (d1 - d0) * (temp - t0) / (t1 - t0);
        }
    }
    return c[n-1].duty;
}

/* Parse "t:d,t:d,..." into a curve (clamped + sorted). Returns 0 on success. */
static int parse_curve(const char *s, curve_t *c) {
    int n = 0;
    while (*s && n < CURVE_MAX) {
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        int t, d, len = 0;
        if (sscanf(s, "%d:%d%n", &t, &d, &len) != 2 || len == 0) break;
        s += len;
        if (t < 0) t = 0; else if (t > 120) t = 120;
        if (d < 0) d = 0; else if (d > DUTY_MAX) d = DUTY_MAX;
        c->pts[n].temp = t; c->pts[n].duty = d; n++;
    }
    if (n < 1) return -1;
    for (int i = 1; i < n; i++) {                 /* insertion sort by temp */
        point_t k = c->pts[i]; int j = i - 1;
        while (j >= 0 && c->pts[j].temp > k.temp) { c->pts[j+1] = c->pts[j]; j--; }
        c->pts[j+1] = k;
    }
    c->n = n;
    return 0;
}

static void set_curve(curve_t *c, const point_t *src, int n) {
    c->n = n;
    for (int i = 0; i < n; i++) c->pts[i] = src[i];
}

/* Built-in defaults (used for any curve the config doesn't override). Tuned on
 * a real iDX6011: quiet stock-like idle floor held flat past typical idle, then
 * ramps to full before the critical thresholds. */
static void config_defaults(fanconf_t *cf, int cli_force) {
    cf->mode = MODE_DEFAULT;
    cf->interval = 3;
    cf->force = cli_force;
    static const point_t cs[] = {{0,18},{64,18},{74,70},{82,140},{88,198}};
    static const point_t cd[] = {{0,24},{60,24},{70,75},{78,140},{86,198}};
    static const point_t ct[] = {{0,50},{55,50},{66,130},{75,185},{82,198}};
    static const point_t ss[] = {{0,40},{49,40},{53,95},{57,160},{60,198}};
    static const point_t sd[] = {{0,55},{48,55},{52,110},{56,170},{60,198}};
    static const point_t st[] = {{0,95},{47,95},{51,150},{55,190},{58,198}};
    set_curve(&cf->cpu[MODE_SILENT], cs, 5);
    set_curve(&cf->cpu[MODE_DEFAULT], cd, 5);
    set_curve(&cf->cpu[MODE_TURBO], ct, 5);
    set_curve(&cf->sys[MODE_SILENT], ss, 5);
    set_curve(&cf->sys[MODE_DEFAULT], sd, 5);
    set_curve(&cf->sys[MODE_TURBO], st, 5);
}

/* Reset to defaults, then apply overrides from the config file. Called at
 * startup and whenever the file changes (so edits hot-reload). */
static void load_config(fanconf_t *cf, int cli_force) {
    config_defaults(cf, cli_force);
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[192];
        if (sscanf(line, " %63[^= ] = %191[^\n\r]", key, val) != 2) continue;
        size_t L = strlen(val);
        while (L && (val[L-1] == ' ' || val[L-1] == '\t' || val[L-1] == '\r')) val[--L] = 0;
        if (!strcmp(key, "mode")) {
            if (!strcmp(val, "silent")) cf->mode = MODE_SILENT;
            else if (!strcmp(val, "turbo") || !strcmp(val, "performance")) cf->mode = MODE_TURBO;
            else cf->mode = MODE_DEFAULT;
        } else if (!strcmp(key, "interval")) {
            int v = atoi(val); if (v >= 1 && v <= 60) cf->interval = v;
        } else if (!strcmp(key, "force")) {
            cf->force = atoi(val);
        }
        else if (!strcmp(key, "cpu_silent"))  parse_curve(val, &cf->cpu[MODE_SILENT]);
        else if (!strcmp(key, "cpu_default")) parse_curve(val, &cf->cpu[MODE_DEFAULT]);
        else if (!strcmp(key, "cpu_turbo"))   parse_curve(val, &cf->cpu[MODE_TURBO]);
        else if (!strcmp(key, "sys_silent"))  parse_curve(val, &cf->sys[MODE_SILENT]);
        else if (!strcmp(key, "sys_default")) parse_curve(val, &cf->sys[MODE_DEFAULT]);
        else if (!strcmp(key, "sys_turbo"))   parse_curve(val, &cf->sys[MODE_TURBO]);
    }
    fclose(f);
}

static void write_status(fan_mode_t mode, int ct, int st,
                         long c1, long c2, long s1, long s2, int cd, int sd) {
    char tmp[] = STATUS_PATH ".tmp";
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "mode=%s\ncpu_temp=%d\nsys_temp=%d\n"
               "cpufan1=%ld\ncpufan2=%ld\nsysfan1=%ld\nsysfan2=%ld\n"
               "cpu_duty=%d\nsys_duty=%d\n",
            mode_name(mode), ct, st, c1, c2, s1, s2, cd, sd);
    fclose(f);
    rename(tmp, STATUS_PATH);
}

/* ---- safety ---- */
static int dmi_is_supported(void) {
    char name[128] = "";
    FILE *f = fopen("/sys/class/dmi/id/product_name", "r");
    if (f) { if (!fgets(name, sizeof(name), f)) name[0] = 0; fclose(f); }
    return strstr(name, "iDX6011") != NULL;
}

static void on_signal(int s) { (void)s; running = 0; }

int main(int argc, char **argv) {
    int cli_force = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--force")) cli_force = 1;

    fanconf_t cf;
    load_config(&cf, cli_force);

    if (!dmi_is_supported() && !cf.force) {
        fprintf(stderr, "ug-fand: this is not a UGREEN iDX6011 (DMI product_name). "
                        "Refusing to poke the EC. Use --force or set force=1 in "
                        CONFIG_PATH " to override.\n");
        return 2;
    }

    if (ioperm(EC_DATA, 1, 1) || ioperm(EC_SC, 1, 1)) {
        fprintf(stderr, "ug-fand: ioperm failed (%s) — must run as root.\n", strerror(errno));
        return 1;
    }

    lock_fd = open(EC_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock_fd < 0)
        fprintf(stderr, "ug-fand: warning: cannot open %s (%s) — EC access unserialized\n",
                EC_LOCK_PATH, strerror(errno));

    mkdir(STATUS_DIR, 0755);

    struct sigaction sa = { .sa_handler = on_signal };
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    fprintf(stderr, "ug-fand: started, mode=%s, interval=%ds\n",
            mode_name(cf.mode), cf.interval);

    time_t cfg_mtime = 0;
    double cpu_ema = -1, sys_ema = -1;   /* smoothed temps (kills brief spikes) */
    int applied_cd = -1, applied_sd = -1;
    while (running) {
        /* hot-reload mode/interval/curves when the config file changes */
        struct stat sb;
        if (stat(CONFIG_PATH, &sb) == 0 && sb.st_mtime != cfg_mtime) {
            cfg_mtime = sb.st_mtime;
            fan_mode_t prev = cf.mode;
            load_config(&cf, cli_force);
            if (cf.mode != prev)
                fprintf(stderr, "ug-fand: mode -> %s\n", mode_name(cf.mode));
            applied_cd = applied_sd = -1;   /* snap to the (possibly new) curves */
        }

        int ct = cpu_temp(), st = sys_temp();

        /* EMA-smooth so brief CPU spikes don't make the fans ramp up and down. */
        if (ct >= 0) cpu_ema = cpu_ema < 0 ? ct : cpu_ema + (ct - cpu_ema) * 0.2;
        if (st >= 0) sys_ema = sys_ema < 0 ? st : sys_ema + (st - sys_ema) * 0.2;
        int cts = cpu_ema < 0 ? -1 : (int)(cpu_ema + 0.5);
        int sts = sys_ema < 0 ? -1 : (int)(sys_ema + 0.5);

        int cd = cts < 0 ? DUTY_MAX : curve_duty(&cf.cpu[cf.mode], cts);
        int sd = sts < 0 ? DUTY_MAX : curve_duty(&cf.sys[cf.mode], sts);
        if (ct >= CPU_CRIT) cd = DUTY_MAX;                  /* failsafe on RAW temp */
        if (st >= SYS_CRIT) sd = DUTY_MAX;

        /* Deadband: hold the current duty for small target changes (anti-hunt);
         * always honour a jump to full. */
        if (applied_cd < 0 || cd == DUTY_MAX || abs(cd - applied_cd) >= DUTY_DEADBAND)
            applied_cd = cd;
        if (applied_sd < 0 || sd == DUTY_MAX || abs(sd - applied_sd) >= DUTY_DEADBAND)
            applied_sd = sd;

        set_fan_pair(REG_CPU_EN1, applied_cd);
        set_fan_pair(REG_SYS_EN1, applied_sd);

        write_status(cf.mode, ct, st,
                     fan_rpm(REG_CPUFAN1_RPM), fan_rpm(REG_CPUFAN2_RPM),
                     fan_rpm(REG_SYSFAN1_RPM), fan_rpm(REG_SYSFAN2_RPM),
                     applied_cd, applied_sd);

        for (int slept = 0; slept < cf.interval && running; slept++) sleep(1);
    }

    /* failsafe on exit: leave fans at a safe, audible level */
    set_fan_pair(REG_CPU_EN1, 140);
    set_fan_pair(REG_SYS_EN1, 140);
    fprintf(stderr, "ug-fand: stopped (fans set to failsafe duty 140)\n");
    return 0;
}
