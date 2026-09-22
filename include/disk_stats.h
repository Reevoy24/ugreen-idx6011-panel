#ifndef DISK_STATS_H
#define DISK_STATS_H

#include <stddef.h>

/* 8 SATA bays + 2 M.2 on a Pro, plus headroom for drives that only an external
 * source reports (see disk_stats_set_external) */
#define DISK_MAX 12

typedef struct {
    char dev[16];     /* kernel name: sda, nvme0n1 — or the name the external source reported */
    int is_nvme;
    int idx;          /* 1-based per type, for display names */
    float size_tb;    /* 0 = unknown (external sources may omit it) */
    float temp_c;     /* < 0 = unknown */
    int online;
} disk_info_t;

typedef struct {
    int count;
    disk_info_t disks[DISK_MAX];
} disk_stats_t;

int disk_stats_collect(disk_stats_t *out);

/* Unraid only: drive temps as already collected by emhttpd
 * (/var/local/emhttp/disks.ini) — reading them costs no disk I/O, unlike
 * drivetemp, whose SMART query can audibly unpark HDD heads on every poll.
 * Returns -1 when the file is absent (not Unraid), else the number of drives
 * listed. *max_c is set to the hottest reported temp in whole °C, or -1 when
 * no listed drive reports one (spun-down drives report temp="*"). */
int disk_stats_unraid_max(int *max_c);

/* ---- external temperature source (opt-in, every platform) ----
 * Generalisation of the Unraid path above: a plain text file that something
 * else keeps up to date, one line per drive:
 *
 *     sda=41          # whole °C, decimals allowed
 *     sdb=39,16.0     # optional second field: capacity in TB
 *     sdc=*           # spun down / no reading ("-" works too)
 *     max=44          # optional aggregate, for a helper that only knows the hottest
 *
 * It exists for drives the host cannot see at all — most commonly a pool whose
 * HBA is passed through to a VM, where the disks live behind VFIO and the host
 * has neither a block device nor SMART for them, while the fans hang off the
 * host's EC. A helper in the VM keeps the file current; ug-fand regulates on it
 * and the panel lists those drives alongside the local ones.
 *
 * Because that helper can die (or its VM reboot) while the pool keeps heating,
 * the file is only trusted while it is fresh: older than max_age seconds counts
 * as no reading at all, which trips the missing-sensor failsafe. */
#define DISK_EXT_OFF   (-1)   /* no external file configured */
#define DISK_EXT_STALE (-2)   /* configured, but missing / stale / unusable */

typedef struct {
    char name[16];
    float temp_c;    /* < 0 = spun down / unknown */
    float size_tb;   /* 0 = not reported */
} disk_ext_t;

/* path "" (or NULL) disables the source; max_age 0 disables the freshness
 * check. Both daemons call this on every config (re)load. */
void disk_stats_set_external(const char *path, int max_age);

/* Hottest externally reported drive. Returns DISK_EXT_OFF, DISK_EXT_STALE, or
 * the number of readings; *max_c is the hottest whole °C, or -1 when every
 * reported drive is spun down. */
int disk_stats_external_max(int *max_c);

/* Push transport (HTTP): validate `text` — the file format above — and write it
 * to the configured external file atomically. 0 on success and *drives set to
 * the accepted reading count; -1 when the body is unusable (the caller's fault),
 * -2 when nothing is configured or the write failed. err gets the reason. */
int disk_stats_write_external(const char *text, int *drives, char *err, size_t errsz);

#endif
