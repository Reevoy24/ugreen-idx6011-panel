#include "gpu_stats.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <i915_drm.h>

static int pmu_fd = -1;
static unsigned long long prev_busy_ns;
static struct timespec prev_ts;
static int have_prev = 0;

int gpu_stats_init(void)
{
    FILE *fp = fopen("/sys/bus/event_source/devices/i915/type", "r");
    if (!fp) {
        fprintf(stderr, "gpu: i915 PMU not present — GPU usage unavailable\n");
        return -1;
    }
    int type = 0;
    int n = fscanf(fp, "%d", &type);
    fclose(fp);
    if (n != 1) return -1;

    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = (unsigned)type;
    attr.size = sizeof(attr);
    attr.config = I915_PMU_ENGINE_BUSY(I915_ENGINE_CLASS_RENDER, 0);

    pmu_fd = (int)syscall(SYS_perf_event_open, &attr, -1, 0, -1, 0);
    if (pmu_fd < 0) {
        fprintf(stderr, "gpu: perf_event_open failed (%s) — GPU usage unavailable\n",
                strerror(errno));
        return -1;
    }
    fprintf(stderr, "gpu: i915 PMU render-busy counter active\n");
    return 0;
}

float gpu_stats_usage(void)
{
    if (pmu_fd < 0) return -1.0f;

    unsigned long long busy_ns = 0;
    if (read(pmu_fd, &busy_ns, sizeof(busy_ns)) != sizeof(busy_ns))
        return -1.0f;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    float pct = -1.0f;
    if (have_prev) {
        double wall_ns = (now.tv_sec - prev_ts.tv_sec) * 1e9 +
                         (now.tv_nsec - prev_ts.tv_nsec);
        if (wall_ns > 0) {
            pct = (float)((busy_ns - prev_busy_ns) / wall_ns * 100.0);
            if (pct < 0.0f) pct = 0.0f;
            if (pct > 100.0f) pct = 100.0f;
        }
    }
    prev_busy_ns = busy_ns;
    prev_ts = now;
    have_prev = 1;
    return pct;
}

void gpu_stats_cleanup(void)
{
    if (pmu_fd >= 0) {
        close(pmu_fd);
        pmu_fd = -1;
    }
    have_prev = 0;
}
