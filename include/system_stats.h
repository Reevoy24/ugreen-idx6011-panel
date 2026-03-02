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

#endif
