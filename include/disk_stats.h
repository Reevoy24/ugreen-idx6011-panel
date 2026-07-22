#ifndef DISK_STATS_H
#define DISK_STATS_H

#define DISK_MAX 8

typedef struct {
    char dev[16];     /* kernel name: sda, nvme0n1 */
    int is_nvme;
    int idx;          /* 1-based per type, for display names */
    float size_tb;
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

#endif
