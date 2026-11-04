# Makefile - IPC demo (Operating Systems Lab, Group 2)
#
#   make            build the program            -> build/ipc_demo
#   make debug      build with -g -O0 for gdb    -> build/ipc_demo_debug
#   make run        build and run                (options:  make run ARGS="-m pipe 1 2 3")
#   make clean      delete build/
#   make cleanipc   remove IPC leftovers after a crash (FIFO file, shared memory)

CC := gcc
WARN := -Wall -Wextra -Wpedantic
CFLAGS_REL := -std=gnu11 $(WARN) -O2
CFLAGS_DBG := -std=gnu11 $(WARN) -O0 -g3
LDLIBS := -pthread -lrt

SRCS := $(wildcard src/*.c)
HDRS := $(wildcard src/*.h)
OBJS_REL := $(patsubst src/%.c,build/release/%.o,$(SRCS))
OBJS_DBG := $(patsubst src/%.c,build/debug/%.o,$(SRCS))

.PHONY: all debug run clean cleanipc

all: build/ipc_demo

debug: build/ipc_demo_debug

build/ipc_demo: $(OBJS_REL)
	$(CC) $(CFLAGS_REL) -o $@ $^ $(LDLIBS)

build/ipc_demo_debug: $(OBJS_DBG)
	$(CC) $(CFLAGS_DBG) -o $@ $^ $(LDLIBS)

build/release/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_REL) -c -o $@ $<

build/debug/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DBG) -c -o $@ $<

run: all
	./build/ipc_demo $(ARGS)

clean:
	rm -rf build

cleanipc:
	rm -f /tmp/ipc_demo_fifo_* /dev/shm/ipc_demo_shm_*
	@echo "Message queues left behind by a crash (if any):  ipcs -q   then   ipcrm -q <msqid>"
