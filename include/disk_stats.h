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

#endif
