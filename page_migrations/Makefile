# Makefile
obj-m += migrate_in_kernel.o

# Get KERNEL_VERSION from environment or default to `uname -r`
KERNEL_VERSION ?= $(shell uname -r)
KDIR := /lib/modules/$(KERNEL_VERSION)/build

# Define EXTRA_CFLAGS or CFLAGS if needed for specific warnings/options
# EXTRA_CFLAGS += -Wno-unused-function -Wno-implicit-fallthrough

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

load: all
	sudo insmod ./migrate_in_kernel.ko target_pid=<YOUR_PID> old_node_id_param=<SOURCE_NODE> new_node_id_param=<TARGET_NODE>
	# Example: sudo insmod ./migrate_in_kernel.ko target_pid=1234 old_node_id_param=0 new_node_id_param=1
	# To migrate within the same node (e.g. node 0):
	# sudo insmod ./migrate_in_kernel.ko target_pid=1234 old_node_id_param=0 new_node_id_param=0

unload:
	sudo rmmod migrate_in_kernel || true # Ignore error if not loaded

log:
	dmesg | tail -n 30

# Example usage:
# 1. Find a PID of a user process (e.g., `pidof bash`)
# 2. Compile: make
# 3. Load: sudo make load target_pid=YOUR_PID (e.g. sudo insmod ./migrate_in_kernel.ko target_pid=1234)
#    You can also specify old_node_id_param and new_node_id_param.
#    To test same-node migration: old_node_id_param=0 new_node_id_param=0 (assuming node 0 exists)
# 4. Check logs: dmesg or sudo make log
# 5. Unload: sudo make unload