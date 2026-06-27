#ifndef FAND_API_H
#define FAND_API_H

/* ug-fand's optional web dashboard + JSON API.
 *
 * A trimmed sibling of the panel's api.c: the SAME hand-rolled HTTP/1.1 server
 * and the SAME web frontend, but fan-only — no display settings, wallpapers or
 * backlight. It lets the *non-Pro* iDX6011 (which has no front display, so it
 * cannot run ug-paneld) get a browser dashboard with system stats and fan
 * control. The server runs in its own pthread, reads a mutex-guarded snapshot
 * the main loop publishes, and writes fan-mode/curve changes back to the
 * ug-fand config (the main loop hot-reloads them). It NEVER pokes the EC. */

#include "system_stats.h"
#include "net_stats.h"
#include "disk_stats.h"

/* Stats snapshot the main loop publishes for the API thread to serve.
 * Mirrors the slice of the panel's api_snapshot_t that the shared frontend
 * actually renders when has_panel is false (Overview / Fans / Storage / Net). */
typedef struct {
    int valid;                  /* 0 until the first poll has filled this in */

    system_stats_t sys;
    net_stats_t    net;
    disk_stats_t   disks;

    /* fan block (mirrors /run/ug-fand/status) */
    int  fan_running;           /* always 1 while the daemon publishes */
    char fan_mode[16];          /* "silent" | "default" | "turbo" */
    int  fan_cpu_temp;          /* deg C, -1 = no reading */
    int  fan_sys_temp;
    int  fan_cpu_pct;           /* applied fan speed %, -1 = unknown */
    int  fan_sys_pct;
    long fan_rpm[4];            /* cpufan1, cpufan2, sysfan1, sysfan2; -1 = n/a */
    char fan_cpu_curve[128];    /* "t:p,t:p,..." for the active mode */
    char fan_sys_curve[128];
    int  fan_crit_cpu;          /* critical-temp thresholds (for the gauges) */
    int  fan_crit_sys;
} fand_snapshot_t;

/* Start/stop the server thread. port<=0 disables it (returns -1). password may
 * be NULL/"" (then the fan-mode control endpoint is open on the LAN). */
int  fand_api_start(int port, const char *password);
void fand_api_stop(void);

/* Main loop publishes the latest stats (cheap mutex'd copy). */
void fand_api_publish(const fand_snapshot_t *s);

/* Implemented in ug_fand.c (it owns the config file): atomically set/replace a
 * "key = value" line in CONFIG_PATH. The main loop's mtime watch hot-reloads it.
 * Returns 0 on success. Used by the /api/fan/mode handler. */
int  fand_config_set(const char *key, const char *value);

#endif
