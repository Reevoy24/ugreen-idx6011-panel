#ifndef GPU_STATS_H
#define GPU_STATS_H

/* Render-engine busy%% via the i915 perf PMU. */
int gpu_stats_init(void);      /* < 0 = unavailable on this system */
float gpu_stats_usage(void);   /* < 0 = no sample yet / unavailable */
void gpu_stats_cleanup(void);

#endif
