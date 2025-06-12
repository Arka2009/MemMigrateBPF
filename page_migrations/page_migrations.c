#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <numa.h>
#include <numaif.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sched.h>
#include <immintrin.h> // For _mm_lfence() on some compilers, or use inline asm
#include "perf_events.h"


// A simple way to create a marker for perf to probe.
// The "nop" instruction is a placeholder that does nothing.
// We can instruct perf to record an event every time this line is executed.
#define PAGE_SIZE 4096
#define MAX_NODES 8
#define NUM_EVENTS 5

struct migration_test {
    void *memory;
    size_t total_size;
    int num_pages;
    void **page_addrs;
    int *target_nodes;
    int *status_before;
    int *status_after;
    struct timespec start_time;
    struct timespec end_time;
};

// You would also need this helper function in your file
int get_kprobe_pmu_type(void) {
    FILE *f = fopen("/sys/bus/event_source/devices/kprobe/type", "r");
    int type = -1;
    if (!f) { return -1; /* error handling */ }
    if (fscanf(f, "%d", &type) != 1) { type = -1; /* error handling */ }
    fclose(f);
    return type;
}

/**
 * touch_migrated_pages_decoupled - Decouples pointer and data accesses.
 *
 * This function first copies all pointers from the heap-allocated page_addrs
 * array into a stack-allocated local array. This "warms up" the TLB for the
 * pointer array, resolving any misses associated with it.
 *
 * It then enters a second loop that ONLY uses the local array to access the
 * migrated pages. By measuring only this second loop, we can isolate the
 * DTLB misses caused exclusively by re-faulting on the migrated data pages.
 */
long touch_migrated_pages_decoupled(struct migration_test *test, int group_fd) {
    volatile long verification_sum = 0;

    // Create a stack-allocated array to hold the pointers.
    // Assumes num_pages is not excessively large to avoid stack overflow.
    void** local_page_addrs = malloc(test->num_pages * sizeof(void*));
    uint64_t num_pages = test->num_pages; 
    if (!local_page_addrs) {
        perror("malloc");
        return -1;
    }

    // --- WARM-UP PHASE: Resolve TLB misses for the pointer array ---
    printf("Warming up TLB for pointer array...\n");
    for (int i = 0; i < test->num_pages; i++) {
        local_page_addrs[i] = test->page_addrs[i];
    }

    // Insert a memory fence to ensure all warm-up loads are complete.
    asm volatile ("mfence" ::: "memory");

    reset_and_enable_ioctl(group_fd);
    for (int i = 0; i < num_pages; i++) {
        // Now we are accessing the local array, which should be hot in the cache/TLB.
        volatile char *page_ptr = (volatile char *)local_page_addrs[i];
        verification_sum += *page_ptr;

        // You can still keep the lfence/cpuid here for maximum serialization.
        asm volatile ("lfence" ::: "memory");
    }

    disable_ioctl(group_fd);

    free(local_page_addrs);

    return verification_sum;
}


/**
 * touch_migrated_pages_lfence - Uses a load fence to serialize reads.
 *
 * By placing an lfence instruction inside the loop, we create a barrier.
 * The CPU's out-of-order engine cannot start processing the load for page (i+1)
 * until the load for page (i) is complete. This reduces the effect of
 * speculative parallel page walks, giving a cleaner signal.
 */
long touch_migrated_pages_lfence(struct migration_test *test) {
    long verification_sum = 0;

    for (int i = 0; i < test->num_pages; i++) {
        volatile char *page_ptr = (volatile char *)test->page_addrs[i];
        verification_sum += *page_ptr; // Perform the read

        // Insert a load fence to serialize memory reads
        asm volatile ("lfence" ::: "memory");
    }

    return verification_sum;
}

/**
 * touch_migrated_pages - Accesses each migrated page to trigger DTLB misses.
 * @test: The migration_test structure containing the page addresses.
 *
 * This function iterates through all pages and performs a single volatile read
 * on each one. The 'volatile' keyword ensures the compiler does not optimize
 * away the memory access. Reading the first byte is sufficient to cause the
 * MMU to resolve the page's physical address, triggering a TLB miss if the
 * translation is not cached. The results are summed and returned to prevent
 * the entire loop from being optimized away as dead code.
 *
 * Returns a checksum of the values read to ensure correctness and prevent optimization.
 */
long touch_migrated_pages(struct migration_test *test) {
    long verification_sum = 0;

    // printf("\nTouching %d pages to measure re-fault penalty...\n", test->num_pages);

    for (int i = 0; i < test->num_pages; i++) {
        // Casting to a volatile pointer is a standard way to tell the compiler
        // that this memory access must happen exactly as written.
        volatile char *page_ptr = (volatile char *)test->page_addrs[i];
        
        // Perform the read. This is the action that will cause a DTLB miss.
        verification_sum += *page_ptr;
    }

    return verification_sum;
}

// Pin to specific CPU to reduce variability
void pin_to_cpu(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("sched_setaffinity");
    }
}

// Allocate memory on specific NUMA node
void* allocate_on_node(size_t size, int node) {
    void *mem;
    
    // Set memory policy to allocate on specific node
    unsigned long nodemask = 1UL << node;
    if (set_mempolicy(MPOL_BIND, &nodemask, node + 2) != 0) {
        perror("set_mempolicy");
        return NULL;
    }
    
    // Allocate and touch memory
    mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    
    // Touch every page to ensure allocation
    for (size_t i = 0; i < size; i += PAGE_SIZE) {
        *((volatile char*)mem + i) = 0x42;
    }
    
    // Reset memory policy
    set_mempolicy(MPOL_DEFAULT, NULL, 0);
    
    return mem;
}

// Initialize migration test structure
struct migration_test* init_migration_test(int num_pages, int source_node, int target_node) {
    struct migration_test *test = calloc(1, sizeof(struct migration_test));
    if (!test) return NULL;
    
    test->num_pages = num_pages;
    test->total_size = num_pages * PAGE_SIZE;
    
    // Allocate arrays
    test->page_addrs = malloc(num_pages * sizeof(void*));
    test->target_nodes = malloc(num_pages * sizeof(int));
    test->status_before = malloc(num_pages * sizeof(int));
    test->status_after = malloc(num_pages * sizeof(int));
    
    if (!test->page_addrs || !test->target_nodes || !test->status_before || !test->status_after) {
        free(test);
        return NULL;
    }
    
    // Allocate memory on source node
    test->memory = allocate_on_node(test->total_size, source_node);
    if (!test->memory) {
        free(test);
        return NULL;
    }
    
    // Set up page addresses and target nodes
    for (int i = 0; i < num_pages; i++) {
        test->page_addrs[i] = (char*)test->memory + i * PAGE_SIZE;
        test->target_nodes[i] = target_node;
    }
    
    return test;
}

// Query current page locations
int query_page_locations(struct migration_test *test, int *status_array) {
    return move_pages(0, test->num_pages, test->page_addrs, NULL, status_array, 0);
}

// Perform the actual migration with timing
int perform_migration(struct migration_test *test) {
    int kprobe_type = get_kprobe_pmu_type();

    if (kprobe_type < 0) {
        exit(EXIT_FAILURE);
    }

    const char *func_to_probe = "remove_migration_pte";
    const char *func_to_probe2 = "handle_mm_fault";
    char* names[NUM_EVENTS] = {
        func_to_probe2,
        func_to_probe,
        "MEM_INST_RETIRED.STLB_MISS_LOADS",
        "DLTB_LOAD_MISSES.COMPLETED_WALKS",
        "TLB Flushes"
    };
    uint64_t types[NUM_EVENTS] = {
        kprobe_type,
        kprobe_type,
        PERF_TYPE_RAW,
        PERF_TYPE_RAW,
        PERF_TYPE_RAW
    };
    uint64_t configs[NUM_EVENTS] = {
        (uint64_t)func_to_probe,
        (uint64_t)func_to_probe,
        0x11D0,
        0x208,
        0x1BD
    };

    struct perf_event_attr pe;
    int fds[NUM_EVENTS];
    uint64_t ids[NUM_EVENTS];
    int group_fd;

    printf("Configuring %d perf events...\n", NUM_EVENTS);
    group_fd = config_perf_multi(&pe, fds, ids, types, configs, kprobe_type, NUM_EVENTS);

    printf("Starting migration of %d pages...\n", test->num_pages);
    
    // Query initial locations
    if (query_page_locations(test, test->status_before) != 0) {
        perror("Failed to query initial page locations");
        return -1;
    }
    
    printf("Before migration - Page locations:\n");
    for (int i = 0; i < test->num_pages; i++) {
        printf("  Page %d: Node %d, VAddr: %p\n", i, test->status_before[i], test->page_addrs[i]);
    }
    
    // Synchronization point - ensure clean start
    sync();
    usleep(1000);  // 1ms settle time
    
    // Start timing
    clock_gettime(CLOCK_MONOTONIC, &test->start_time);
    
    // reset_and_enable_ioctl(group_fd);
    // Perform migration
    int result = move_pages(0, test->num_pages, test->page_addrs, 
                           test->target_nodes, test->status_after, MPOL_MF_MOVE);
    // disable_ioctl(group_fd);
    
    printf("Workload finished.\n");

    // reset_and_enable_ioctl(group_fd);

    touch_migrated_pages_decoupled(test, group_fd);

    // disable_ioctl(group_fd);


    // End timing
    clock_gettime(CLOCK_MONOTONIC, &test->end_time);
    
    if (result != 0) {
        perror("move_pages failed");
        printf("Detailed status:\n");
        for (int i = 0; i < test->num_pages; i++) {
            printf("  Page %d: Status %d\n", i, test->status_after[i]);
        }
        return -1;
    }
    
    // Verify migration
    int *verify_status = malloc(test->num_pages * sizeof(int));
    if (query_page_locations(test, verify_status) == 0) {
        printf("After migration - Page locations:\n");
        for (int i = 0; i < test->num_pages; i++) {
            printf("  Page %d: Node %d, Addr: %p\n", i, verify_status[i], test->page_addrs[i]);
        }
    }
    free(verify_status);

    get_perf(names, ids, NUM_EVENTS, group_fd);
    for (int i = 0; i < NUM_EVENTS; i++) {
        close(fds[i]);
    }
    
    return 0;
}

// Calculate and print timing results
void print_timing_results(struct migration_test *test) {
    long seconds = test->end_time.tv_sec - test->start_time.tv_sec;
    long nanoseconds = test->end_time.tv_nsec - test->start_time.tv_nsec;
    
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000;
    }
    
    double total_time_us = seconds * 1000000.0 + nanoseconds / 1000.0;
    double time_per_page_us = total_time_us / test->num_pages;
    double bandwidth_mbps = (test->total_size / (1024.0 * 1024.0)) / (total_time_us / 1000000.0);
    
    printf("\n=== Timing Results ===\n");
    printf("Total pages migrated: %d\n", test->num_pages);
    printf("Total size: %zu bytes (%.2f MB)\n", test->total_size, test->total_size / (1024.0 * 1024.0));
    printf("Total time: %.2f microseconds\n", total_time_us);
    printf("Time per page: %.2f microseconds\n", time_per_page_us);
    printf("Bandwidth: %.2f MB/s\n", bandwidth_mbps);
    printf("Raw timing: %ld.%09ld -> %ld.%09ld seconds\n", 
           test->start_time.tv_sec, test->start_time.tv_nsec,
           test->end_time.tv_sec, test->end_time.tv_nsec);
}

void cleanup_test(struct migration_test *test) {
    if (test) {
        if (test->memory) munmap(test->memory, test->total_size);
        free(test->page_addrs);
        free(test->target_nodes);
        free(test->status_before);
        free(test->status_after);
        free(test);
    }
}

int main(int argc, char *argv[]) {

    int num_pages = 5;
    int source_node = 1;
    int target_node = 0;
    int cpu_pin = 0;
    
    // Parse command line arguments
    if (argc >= 2) num_pages = atoi(argv[1]);
    if (argc >= 3) source_node = atoi(argv[2]);
    if (argc >= 4) target_node = atoi(argv[3]);
    if (argc >= 5) cpu_pin = atoi(argv[4]);
    
    printf("=== Deterministic Page Migration Test ===\n");
    printf("PID: %d\n", getpid());
    printf("Pages: %d, Source Node: %d, Target Node: %d, CPU Pin: %d\n", 
           num_pages, source_node, target_node, cpu_pin);
    
    if (numa_available() < 0) {
        printf("NUMA not available\n");
        return 1;
    }
    
    if (numa_max_node() < target_node) {
        printf("Target node %d not available (max: %d)\n", target_node, numa_max_node());
        return 1;
    }
    
    // Pin to specific CPU for consistent timing
    pin_to_cpu(cpu_pin);
    
    // Initialize test
    struct migration_test *test = init_migration_test(num_pages, source_node, target_node);
    if (!test) {
        printf("Failed to initialize migration test\n");
        return 1;
    }
    
    // Wait for user input to ensure clean measurement
    printf("\nPress Enter to start migration (this allows you to start tracing tools)...");
    getchar();
    
    // Perform migration
    if (perform_migration(test) == 0) {
        print_timing_results(test);
    }
    
    printf("\nPress Enter to exit (allows you to collect final traces)...");
    getchar();
    
    cleanup_test(test);
    return 0;
}