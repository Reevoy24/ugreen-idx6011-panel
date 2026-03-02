#include "system_stats.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/sysinfo.h>

static unsigned long long prev_total = 0;
static unsigned long long prev_idle = 0;

static float get_cpu_usage(void) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0f;

    char line[256];
    unsigned long long user, nice, sys, idle, iowait = 0, irq = 0, softirq = 0;

    if (!fgets(line, sizeof(line), fp) || strncmp(line, "cpu ", 4) != 0) {
        fclose(fp);
        return 0.0f;
    }
    fclose(fp);

    sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu",
           &user, &nice, &sys, &idle, &iowait, &irq, &softirq);

    unsigned long long total = user + nice + sys + idle + iowait + irq + softirq;
    unsigned long long idle_total = idle + iowait;

    unsigned long long total_diff = total - prev_total;
    unsigned long long idle_diff = idle_total - prev_idle;

    prev_total = total;
    prev_idle = idle_total;

    if (total_diff == 0) return 0.0f;

    return ((float)(total_diff - idle_diff) / (float)total_diff) * 100.0f;
}

static float get_ram_usage(float *used_mb, float *total_mb) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0.0f;

    unsigned long total_kb = 0, free_kb = 0, buffers_kb = 0, cached_kb = 0;
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line, "MemTotal: %lu kB", &total_kb);
        else if (strncmp(line, "MemFree:", 8) == 0)
            sscanf(line, "MemFree: %lu kB", &free_kb);
        else if (strncmp(line, "Buffers:", 8) == 0)
            sscanf(line, "Buffers: %lu kB", &buffers_kb);
        else if (strncmp(line, "Cached:", 7) == 0)
            sscanf(line, "Cached: %lu kB", &cached_kb);
    }
    fclose(fp);

    if (total_kb == 0) return 0.0f;

    unsigned long used_kb = total_kb - (free_kb + buffers_kb + cached_kb);

    if (used_mb) *used_mb = (float)used_kb / 1024.0f;
    if (total_mb) *total_mb = (float)total_kb / 1024.0f;

    return ((float)used_kb / (float)total_kb) * 100.0f;
}

static float get_disk_usage(float *used_gb, float *total_gb) {
    FILE *fp = popen("df -BG / 2>/dev/null | tail -1", "r");
    if (!fp) return 0.0f;

    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        pclose(fp);
        return 0.0f;
    }
    pclose(fp);

    char mount[64], total[32], used[32], avail[32], usepct[32];
    sscanf(line, "%63s %31s %31s %31s %31s",
           mount, total, used, avail, usepct);

    float total_val = 0, used_val = 0;
    sscanf(total, "%f", &total_val);
    sscanf(used, "%f", &used_val);

    if (used_gb) *used_gb = used_val;
    if (total_gb) *total_gb = total_val;

    int pct = 0;
    sscanf(usepct, "%d%%", &pct);
    return (float)pct;
}

static float get_cpu_temp(void) {
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp) return -1.0f;

    int millideg = 0;
    if (fscanf(fp, "%d", &millideg) != 1) {
        fclose(fp);
        return -1.0f;
    }
    fclose(fp);

    return (float)millideg / 1000.0f;
}

static uint64_t get_uptime_seconds(void) {
    struct sysinfo info;
    if (sysinfo(&info) != 0) return 0;
    return (uint64_t)info.uptime;
}

int system_stats_collect(system_stats_t *stats) {
    if (!stats) return -1;

    stats->cpu_usage = get_cpu_usage();
    stats->ram_usage = get_ram_usage(&stats->ram_used_mb, &stats->ram_total_mb);
    stats->disk_usage = get_disk_usage(&stats->disk_used_gb, &stats->disk_total_gb);
    stats->temp_c = get_cpu_temp();
    stats->uptime_seconds = get_uptime_seconds();

    return 0;
}
