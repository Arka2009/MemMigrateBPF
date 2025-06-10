# MemMigrateBPF

## Building bpftool
```
sudo apt install clang-15 llvm-15-dev clang-tools-15

DESTDIR=/home/amaity/Desktop/BPF \
CC=/usr/bin/clang-15 \
CXX=/usr/bin/clang++-15 \
make install -C src -j 4 > /home/amaity/Desktop/MemMigrateBPF/Make.log
```

## Cloning linux kernel
```bash
git clone -b v6.8 --depth=3 https://github.com/torvalds/linux.git

sudo ./bpftool perf show > PerfFeats.txt
sudo chown $USER:$USER PerfFeats.txt
```

## Important migration related tracepoint
```
migrate:remove_migration_pte
migrate:set_migration_pte
migrate:mm_migrate_pages_start
migrate:mm_migrate_pages
syscalls:sys_exit_migrate_pages
syscalls:sys_enter_migrate_pages
```
Use the `cat /sys/kernel/tracing/events/migrate/mm_migrate_pages/format`, to check the ctx attrs associated with the tracepoint.

Some of the ctx datastructure are defined below
```
trace_event_raw_mm_migrate_pages
trace_event_raw_mm_migrate_pages_start
```

## Running sample application and the collector
```bash
gcc memrdwr.c -o memrdwr
./memrdwr # Note the pid after the display


```


## NUMA command
```bash

export pid=104579

# Check process NUMA stats
numastat -p $pid

# Migrate pages
migratepages $pid 1 0
migratepages $pid 0 1


sudo ./migrate_lat_user -p $(migratepages $pid 1 0)
```