#ifndef __MIGRATE_LAT_H
#define __MIGRATE_LAT_H

/* ------------- Ring-buffer event sent to userland ------------ */
struct lat_event {
    char comm[16];
    __u32 pid;
    __u64 delta_ns;
    __u64 pages_ok;
    __u64 pages_failed;
    __u32 mode;
    __u32 reason;
};

#endif /* __MIGRATE_LAT_H */