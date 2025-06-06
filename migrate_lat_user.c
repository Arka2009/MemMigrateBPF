// gcc -O2 -g -Wall migrate_lat_user.c -o migrate_lat_user \
//       -I/usr/include/ -lbpf -lelf -lz
#include <stdio.h>
#include <signal.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include "migrate_lat.h"
#include "migrate_lat.skel.h"

static volatile bool stop = false;
static void handle_int(int sig) { stop = true; }

static int handle_event(void *ctx, void *data, size_t sz)
{
        const struct lat_event *e = data;
        printf("%-16s %-6u  %9.3f ms  ok=%-5llu  fail=%-5llu  mode=%u  reason=%u\n",
               e->comm, e->pid, e->delta_ns / 1e6,
               e->pages_ok, e->pages_failed, e->mode, e->reason);
        return 0;
}

int main(void)
{
        struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
        setrlimit(RLIMIT_MEMLOCK, &r);

        struct migrate_lat_bpf *skel = migrate_lat_bpf__open();
        if (!skel) { perror("open"); return 1; }

        if (migrate_lat_bpf__load(skel) || migrate_lat_bpf__attach(skel)) {
            fprintf(stderr, "load/attach failed\n");
            return 1;
        }

        // Create ring buffer using the 'events' map
        struct ring_buffer *rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
        if (!rb) {
            fprintf(stderr, "Failed to create ring buffer\n");
            migrate_lat_bpf__destroy(skel);
            return 1;
        }

        signal(SIGINT, handle_int);
        signal(SIGTERM, handle_int);
        printf("%-16s %-6s %-11s %-9s %-9s %-6s %-6s\n",
               "COMM", "PID", "LAT(ms)", "OK", "FAIL", "MODE", "RSN");

        while (!stop) {
            ring_buffer__poll(rb, 100);
        }
        
        ring_buffer__free(rb);
        migrate_lat_bpf__destroy(skel);
        return 0;
}
