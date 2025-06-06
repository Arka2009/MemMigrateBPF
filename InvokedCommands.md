```bash
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

clang -O2 -g -target bpf -I/usr/src/linux-headers-6.8.0-60-generic/arch/x86/include -I/usr/src/linux-headers-6.8.0-60-generic/arch/x86/include/generated -I/usr/src/linux-headers-6.8.0-60-generic/include -I/usr/src/linux-headers-6.8.0-60-generic/include/uapi -I/usr/src/linux-headers-6.8.0-60-generic/include/generated/uapi -I/usr/src/linux-headers-6.8.0-60-generic/arch/x86/include/uapi -I/usr/src/linux-headers-6.8.0-60-generic/arch/x86/include/generated/uapi -I/usr/src/linux-headers-6.8.0-60-generic/tools/bpf/resolve_btfids/libbpf/include -c migrate_lat.bpf.c -o migrate_lat.bpf.o

bpftool gen skeleton migrate_lat.bpf.o > migrate_lat.skel.h

gcc -O2 -g -I/home/amaity/Desktop/MMigratePlugin -I/usr/src/linux-headers-6.8.0-60-generic/tools/bpf/resolve_btfids/libbpf/include migrate_lat_user.c -o migrate_lat_user -lbpf -lelf -lz
```