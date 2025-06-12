#ifndef PERF_EVENTS_TRACKER
#define PERF_EVENTS_TRACKER
#define _GNU_SOURCE
/*for performance tracking*/
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct value {
  unsigned long value;
  unsigned long id;
};
struct read_format {
  unsigned long nr;
  struct value values[];
};

/*
#ifdef RISCV64
#define SINGLE_MASK 0x1
#define MEMSYS_EVENTS 0x2
#elif __aarch64__
#define L1D_CACHE 0x04
#define L1D_CACHE_REFILL 0x03
#define L2D_CACHE 0x16
#define L2D_CACHE_REFILL 0x17
#define L3D_CACHE 0x2B
#define L3D_CACHE_REFILL 0x2A
#endif
*/
uint64_t config_cache_id(uint64_t perf_hw_cache_id, uint64_t perf_hw_cache_op_id, uint64_t perf_hw_cache_op_result_id);
void reset_and_enable_ioctl(int fd);
void disable_ioctl(int fd);
void config_perf(struct perf_event_attr *pe,int *fd,uint64_t type, uint64_t config);
int config_perf_multi(struct perf_event_attr *pe, int *fds, uint64_t *ids, uint64_t *types, uint64_t *configs, int kprobe_pmu, int event_count);
void get_perf(char **string, uint64_t *ids, int event_count, int group_fd);
#endif