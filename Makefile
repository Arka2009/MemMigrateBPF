CLANG ?= clang
BPFTOOL ?= bpftool
LIBBPF_SRC ?= /usr/include
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_HEADERS ?= /usr/src/linux-headers-$(KERNEL_VERSION)
ARCH ?= $(shell uname -m | sed 's/x86_64/x86/')
BPF_OBJ   = migrate_lat.bpf.o
SKEL_HDR  = migrate_lat.skel.h
VMLINUX_BTF ?= /sys/kernel/btf/vmlinux

# Add include paths to CFLAGS
CFLAGS = -O2 -g

# BPF_IFLAGS = -I$(KERNEL_HEADERS)/tools/bpf/resolve_btfids/libbpf/include
# KERNELHDR_IFLAGS = -I$(KERNEL_HEADERS)/arch/$(ARCH)/include \
#     -I$(KERNEL_HEADERS)/arch/$(ARCH)/include/generated \
#     -I$(KERNEL_HEADERS)/include \
#     -I$(KERNEL_HEADERS)/include/uapi \
#     -I$(KERNEL_HEADERS)/include/generated/uapi \
# 	-I$(KERNEL_HEADERS)/arch/$(ARCH)/include/uapi \
#     -I$(KERNEL_HEADERS)/arch/$(ARCH)/include/generated/uapi
PWD_IFLAGS=-I$(shell pwd)

BPF_FLAGS = $(CFLAGS) -target bpf -D__TARGET_ARCH_x86 $(PWD_IFLAGS)

all: $(SKEL_HDR) migrate_lat_user

# Add this target before your other targets
vmlinux.h:
	$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@

$(BPF_OBJ): migrate_lat.bpf.c vmlinux.h
	$(CLANG) $(BPF_FLAGS) -c $< -o $@

$(SKEL_HDR): $(BPF_OBJ)
	$(BPFTOOL) gen skeleton $< > $@

migrate_lat_user: migrate_lat_user.c $(SKEL_HDR)
	gcc $(CFLAGS) $(PWD_IFLAGS) $(BPF_IFLAGS) $< -o $@ -lbpf -lelf -lz

clean:
	rm -f $(BPF_OBJ) $(SKEL_HDR) vmlinux.h migrate_lat_user
