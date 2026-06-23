#include "gpu_stats.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <i915_drm.h>

/* Sample several i915 engine-busy counters and report the busiest. On a NAS the
 * iGPU is used mostly for video transcoding (VIDEO / VIDEO_ENHANCE engines), so
 * watching only the RENDER engine would read 0% even while Jellyfin etc. are
 * transcoding. We open render + video(0,1) + video-enhance and return the max. */
#define MAX_ENG 6

static int eng_fd[MAX_ENG];
static unsigned long long eng_prev[MAX_ENG];
static int eng_count = 0;
static struct timespec prev_ts;
static int have_prev = 0;

static int open_engine(unsigned type, unsigned cls, unsigned inst)
{
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = type;
    attr.size = sizeof(attr);
    attr.config = I915_PMU_ENGINE_BUSY(cls, inst);
    return (int)syscall(SYS_perf_event_open, &attr, -1, 0, -1, 0);
}

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

    static const struct { unsigned cls, inst; } want[] = {
        { I915_ENGINE_CLASS_RENDER, 0 },
        { I915_ENGINE_CLASS_VIDEO, 0 },
        { I915_ENGINE_CLASS_VIDEO, 1 },
        { I915_ENGINE_CLASS_VIDEO_ENHANCE, 0 },
    };
    eng_count = 0;
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]) && eng_count < MAX_ENG; i++) {
        int fd = open_engine((unsigned)type, want[i].cls, want[i].inst);
        if (fd >= 0) eng_fd[eng_count++] = fd;
    }
    if (eng_count == 0) {
        fprintf(stderr, "gpu: perf_event_open failed (%s) — GPU usage unavailable\n", strerror(errno));
        return -1;
    }
    fprintf(stderr, "gpu: i915 PMU active (%d engine counters: render/video)\n", eng_count);
    return 0;
}

float gpu_stats_usage(void)
{
    if (eng_count == 0) return -1.0f;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double wall_ns = have_prev ? (now.tv_sec - prev_ts.tv_sec) * 1e9 +
                                 (now.tv_nsec - prev_ts.tv_nsec) : 0.0;

    float best = -1.0f;
    for (int i = 0; i < eng_count; i++) {
        unsigned long long busy = 0;
        if (read(eng_fd[i], &busy, sizeof(busy)) != sizeof(busy)) continue;
        if (have_prev && wall_ns > 0) {
            float pct = (float)((busy - eng_prev[i]) / wall_ns * 100.0);
            if (pct < 0.0f) pct = 0.0f;
            if (pct > 100.0f) pct = 100.0f;
            if (pct > best) best = pct;
        }
        eng_prev[i] = busy;
    }
    prev_ts = now;
    have_prev = 1;
    return best; /* -1 on the first call (no delta yet), then the busiest engine */
}

void gpu_stats_cleanup(void)
{
    for (int i = 0; i < eng_count; i++)
        if (eng_fd[i] >= 0) close(eng_fd[i]);
    eng_count = 0;
    have_prev = 0;
}
