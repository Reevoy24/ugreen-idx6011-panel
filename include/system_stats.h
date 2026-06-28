#ifndef SYSTEM_STATS_H
#define SYSTEM_STATS_H

#include <stdint.h>

typedef struct {
    float cpu_usage;
    float ram_usage;
    float ram_used_mb;
    float ram_total_mb;
    float disk_usage;
    float disk_used_gb;
    float disk_total_gb;
    float temp_c;
    uint64_t uptime_seconds;
} system_stats_t;

int system_stats_collect(system_stats_t *stats);

/* Set the mountpoint whose usage the Storage widget reports (default "/").
 * Pass a pool/data mountpoint on TrueNAS (e.g. "/mnt/tank") so the widget shows
 * the data pool instead of the read-only boot pool. NULL or "" resets to "/". */
void system_stats_set_root(const char *path);

#define STORAGE_OPT_LEN 256   /* max mountpoint string in the Storage picker */
#define STORAGE_OPT_MAX 12    /* mountpoints offered in the picker */

/* List candidate Storage mountpoints for the picker: "/" plus each top-level
 * /mnt/<name> (real, non-pseudo filesystems). If `current` is non-empty it is
 * guaranteed to be in the list, and *cur_idx is set to its index (else 0).
 * Returns the count (<= max). */
int system_stats_list_mounts(char out[][STORAGE_OPT_LEN], int max,
                             const char *current, int *cur_idx);

#endif
