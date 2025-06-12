#include "perf_events.h"

void reset_and_enable_ioctl(int fd){
    int err = ioctl(fd, PERF_EVENT_IOC_RESET,PERF_IOC_FLAG_GROUP);
    if (err){
        perror("reset_and_enable_ioctl");
        exit(EXIT_FAILURE);
    }
    err = ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    if (err){
        perror("reset_and_enable_ioctl");
        exit(EXIT_FAILURE);
    }
}

void disable_ioctl(int fd){
    int err = ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    if (err){
        perror("reset_and_enable_ioctl");
        exit(EXIT_FAILURE);
    }
}

void config_perf(struct perf_event_attr *pe,int *fd,uint64_t type, uint64_t config){
    // memset(pe, 0, sizeof(*pe));
    pe->type = type;
    pe->size = sizeof(*pe);
    pe->config = config;
    pe->disabled = 1;
    pe->exclude_kernel = 0;
    pe->exclude_hv = 1;
    *fd = syscall(__NR_perf_event_open, pe, 0, -1, -1, 0);
}

uint64_t config_cache_id(uint64_t perf_hw_cache_id, uint64_t perf_hw_cache_op_id, uint64_t perf_hw_cache_op_result_id){
    return (perf_hw_cache_id) | 
    (perf_hw_cache_op_id << 8) | 
    (perf_hw_cache_op_result_id << 16);
}

/*
The group_fd argument allows event groups to be created. An
event group has one event which is the group leader. The leader
is created first, with group_fd = -1. The rest of the group
members are created with subsequent perf_event_open() calls with
group_fd being set to the file descriptor of the group leader.
(A single event on its own is created with group_fd = -1 and is
considered to be a group with only 1 member.) An event group is
scheduled onto the CPU as a unit: it will be put onto the CPU
only if all of the events in the group can be put onto the CPU.
This means that the values of the member events can be
meaningfully compared—added, divided (to get ratios), and so on—
with each other, since they have counted events for the same set
of executed instructions.

Inputs:
struct perf_event_attr pe;
int fds[NUM_EVENTS];       // Array for file descriptors
uint64_t ids[NUM_EVENTS];  // Array for IDs
uint64_t types[NUM_EVENTS];
uint64_t configs[NUM_EVENTS];
*/
// The new, more flexible config_perf_multi function
int config_perf_multi(struct perf_event_attr *pe, int *fds, uint64_t *ids,
    uint64_t *types, uint64_t *configs,
    int kprobe_pmu, // Pass in the kprobe PMU type
    int event_count) {
    int group_fd = -1;

    for (int i = 0; i < event_count; i++) {
        memset(pe, 0, sizeof(struct perf_event_attr));
        pe->size = sizeof(struct perf_event_attr);
        pe->type = types[i];
        pe->disabled = 1;
        pe->exclude_kernel = 1;
        pe->exclude_hv = 1;
        pe->read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;

    // --- THIS IS THE NEW LOGIC ---
    // Check if the current event is a kprobe
        if (pe->type == kprobe_pmu) {
            // For a kprobe, the 'config' array element holds the string pointer
            pe->kprobe_func = configs[i]; // This is a (uint64_t)char*
            pe->probe_offset = 0;         // Probe the function entry
            } else {
            // For all other event types, 'config' is the event ID
            pe->config = configs[i];
        }
        // --- END OF NEW LOGIC ---
    
        // The rest of the function is the same as your corrected version
        int current_fd = syscall(__NR_perf_event_open, pe, 0, -1, group_fd, 0);
        if (current_fd < 0) {
            fprintf(stderr, "Error opening event #%d (type: %lu, config: %lx): ", i, types[i], configs[i]);
            perror("perf_event_open");
            exit(EXIT_FAILURE);
        }
    
        fds[i] = current_fd;
        if (i == 0) {
        group_fd = current_fd;
        }
        ioctl(current_fd, PERF_EVENT_IOC_ID, &ids[i]);
    }
    return group_fd;
}
// CORRECTED: Buffer size calculation, struct definition, and print loop were fixed.
void get_perf(char **string, uint64_t *ids, int event_count, int group_fd) {
    // Disable the group before reading
    disable_ioctl(group_fd);

    // Calculate buffer size needed for the read
    size_t buffer_size = sizeof(uint64_t) + (event_count * sizeof(uint64_t) * 2);
    char *buf = malloc(buffer_size);
    if (!buf) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    if (read(group_fd, buf, buffer_size) < 0) {
        perror("read");
        free(buf);
        exit(EXIT_FAILURE);
    }

    struct read_format *rf = (struct read_format *)buf;

    printf("\n--- Perf Statistics ---\n");
    // Match the read values back to their names using the IDs
    for (size_t i = 0; i < rf->nr; i++) {
        for (int j = 0; j < event_count; j++) {
            if (rf->values[i].id == ids[j]) {
                // CORRECTED: Use %llu for uint64_t and correct string array index
                printf("%-25s: %lu\n", string[j], rf->values[i].value);
                break; // Found it, move to the next value
            }
        }
    }
    printf("-----------------------\n");
    free(buf);
}