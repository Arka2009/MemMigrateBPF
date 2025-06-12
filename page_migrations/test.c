#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include our new header file
#include "perf_events.h"

#define NUM_EVENTS 4

int main(void) {
    char* names[NUM_EVENTS] = {
        "CPU Cycles",
        "Instructions",
        "L1D Cache Loads",
        "L1D Cache Load Misses"
    };
    uint64_t types[NUM_EVENTS] = {
        PERF_TYPE_HARDWARE,
        PERF_TYPE_HARDWARE,
        PERF_TYPE_HW_CACHE,
        PERF_TYPE_HW_CACHE
    };
    uint64_t configs[NUM_EVENTS] = {
        PERF_COUNT_HW_CPU_CYCLES,
        PERF_COUNT_HW_INSTRUCTIONS,
        config_cache_id(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_ACCESS),
        config_cache_id(PERF_COUNT_HW_CACHE_L1D, PERF_COUNT_HW_CACHE_OP_READ, PERF_COUNT_HW_CACHE_RESULT_MISS)
    };

    struct perf_event_attr pe;
    int fds[NUM_EVENTS];
    uint64_t ids[NUM_EVENTS];
    int group_fd;

    printf("Configuring %d perf events...\n", NUM_EVENTS);
    group_fd = config_perf_multi(&pe, fds, ids, types, configs, NUM_EVENTS);

    printf("Starting workload...\n");
    reset_and_enable_ioctl(group_fd);

    // --- YOUR WORKLOAD GOES HERE ---
    volatile int total = 0;
    for (long i = 0; i < 10000000; i++) {
        total += i;
    }
    // -------------------------------

    get_perf(names, ids, NUM_EVENTS, group_fd);
    printf("Workload finished.\n");

    for (int i = 0; i < NUM_EVENTS; i++) {
        close(fds[i]);
    }

    return 0;
}