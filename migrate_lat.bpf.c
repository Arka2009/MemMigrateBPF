// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include "migrate_lat.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/* ------------- Key for the start-timestamp hash ------------- */
struct start_key {
        u32  pid;
        u64  cgroup_id;   /* helps when the same task nests containers */
};

/* -------- BPF maps -------- */
struct {
        __uint(type, BPF_MAP_TYPE_HASH);
        __uint(max_entries, 8192);
        __type(key, struct start_key);
        __type(value, u64);             /* start timestamp */
} starts SEC(".maps");

struct {
        __uint(type, BPF_MAP_TYPE_RINGBUF);
        __uint(max_entries, 1 << 24);
} events SEC(".maps");

/* ---------- Helpers ---------- */
static __always_inline struct start_key make_key(void)
{
        struct start_key k = {};
        k.pid        = bpf_get_current_pid_tgid() >> 32;
        k.cgroup_id  = bpf_get_current_cgroup_id();
        return k;
}

/* ---------- kprobe entry: remember T0 ---------- */
// SEC("kprobe/migrate_pages")
// int BPF_KPROBE(handle_migrate_pages_entry)
// {
//         struct start_key k   = make_key();
//         u64 ts               = bpf_ktime_get_ns();
//         bpf_map_update_elem(&starts, &k, &ts, BPF_ANY);
//         return 0;
// }

SEC("tp/migrate/mm_migrate_pages_start")
int handle_mm_migrate_pages_start(struct trace_event_raw_mm_migrate_pages_start *ctx) {
        struct start_key k   = make_key();
        u64 ts               = bpf_ktime_get_ns();
        bpf_map_update_elem(&starts, &k, &ts, BPF_ANY);
        return 0;
}

/* ---------- trace-point after migration returns: compute Δt ---- */

// SEC("kretprobe/migrate_pages")
// int BPF_KRETPROBE(handle_migrate_pages_exit, int ret)
// {
//         struct start_key k = make_key();
//         u64 *tsp = bpf_map_lookup_elem(&starts, &k);
//         if (!tsp)
//                 return 0;

//         u64 delta = bpf_ktime_get_ns() - *tsp;
//         bpf_map_delete_elem(&starts, &k);

//         struct lat_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
//         if (!e)
//                 return 0;

//         e->pid = k.pid;
//         e->delta_ns = delta;
        
//         // Note: We can't get the exact succeeded/failed pages count from kretprobe
//         // ret value is negative on error, or number of pages migrated on success
//         if (ret < 0) {
//             e->pages_ok = 0;
//             e->pages_failed = 1;  // indicating error
//         } else {
//             e->pages_ok = ret;    // number of pages migrated
//             e->pages_failed = 0;
//         }
        
//         // These values aren't available via kretprobe
//         e->mode = 0;
//         e->reason = 0;
        
//         bpf_get_current_comm(&e->comm, sizeof(e->comm));
//         bpf_ringbuf_submit(e, 0);
//         return 0;
// }

SEC("tp/migrate/mm_migrate_pages")
int handle_mm_migrate_pages(struct trace_event_raw_mm_migrate_pages *ctx)
{
        struct start_key k   = make_key();
        u64 *tsp             = bpf_map_lookup_elem(&starts, &k);
        if (!tsp)
                return 0;                       /* unmatched – ignore */

        u64 delta            = bpf_ktime_get_ns() - *tsp;
        bpf_map_delete_elem(&starts, &k);

        struct lat_event *e  = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
        if (!e)
                return 0;

        e->pid          = k.pid;
        e->delta_ns     = delta;
        e->pages_ok     = ctx->succeeded;
        e->pages_failed = ctx->failed;
        e->mode         = ctx->mode;
        e->reason       = ctx->reason;
        bpf_get_current_comm(&e->comm, sizeof(e->comm));

        bpf_ringbuf_submit(e, 0);
        return 0;
}

char LICENSE[] SEC("license") = "GPL";