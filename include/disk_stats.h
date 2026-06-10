#ifndef DISK_STATS_H
#define DISK_STATS_H

#define DISK_MAX 8

typedef struct {
    char name[20];    /* display name: "Festplatte 1", "NVMe 1" */
    char dev[16];     /* kernel name: sda, nvme0n1 */
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
