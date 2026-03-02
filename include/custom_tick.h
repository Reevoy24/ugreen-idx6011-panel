#ifndef CUSTOM_TICK_H
#define CUSTOM_TICK_H

#include <stdint.h>
#include <time.h>

static inline uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;

    if (start_ms == 0)
        start_ms = now_ms;

    return (uint32_t)(now_ms - start_ms);
}

#endif
