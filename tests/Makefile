# SPDX-License-Identifier: GPL-2.0-only
#
# Host-side unit tests: the portable parts of hix (heap, page allocator, IPC,
# VFS, steel, printf) compiled for the build machine and run there.

CC	?= gcc
CFLAGS	:= -std=gnu11 -g -O1 -Wall -Wextra -Wshadow -Wno-unused-parameter \
	   -DHIX_HOST_TEST -fno-builtin -fno-tree-loop-distribute-patterns \
	   -I../include -I.
CXX	?= g++
CXXFLAGS := -std=gnu++17 -g -O1 -Wall -Wextra -Wshadow -Wno-unused-parameter \
	   -DHIX_HOST_TEST -fno-builtin -fno-exceptions -fno-rtti \
	   -fno-threadsafe-statics -I../include -I.
BUILD	:= build

LIB	:= ../lib/string.c ../lib/vsprintf.c
MM	:= ../aether/pmm.c ../aether/kalloc.c
TESTS	:= test_lib test_mm test_ipc test_vfs test_steel test_shell test_gfx test_chess test_cxx test_desk
NASM	?= nasm

.PHONY: run clean
run: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for t in $(TESTS); do echo "== $$t"; $(BUILD)/$$t; done; \
	echo "== test_sched (32-bit, real context switches)"; \
	$(MAKE) --no-print-directory $(BUILD)/test_sched || exit 1; \
	$(BUILD)/test_sched || exit 1; \
	echo; echo "all host tests passed"

$(BUILD)/test_lib: test_lib.c stubs.c $(LIB)
$(BUILD)/test_mm: test_mm.c stubs.c $(LIB) $(MM)
$(BUILD)/test_ipc: test_ipc.c stubs.c $(LIB) $(MM) ../aether/ipc.c
$(BUILD)/test_vfs: test_vfs.c stubs.c $(LIB) $(MM) ../qubix/vfs.c
$(BUILD)/test_steel: test_steel.c stubs.c $(LIB) $(MM) \
	../steel/surface.c ../steel/device.c ../steel/compositor.c \
	../steel/console.c

# C++ parts are compiled to objects first, then linked into the C test
CXXOBJ	:= $(patsubst ../%.cpp,$(BUILD)/%.o,$(wildcard ../apps/*.cpp ../steel/*.cpp))
$(BUILD)/apps/%.o: ../apps/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(BUILD)/steel/%.o: ../steel/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
THEME	:= $(BUILD)/steel/theme.o ../steel/icon_data.c

$(BUILD)/test_chess: test_chess.c stubs.c $(LIB) $(BUILD)/apps/chess_rules.o
$(BUILD)/test_cxx: test_cxx.cpp stubs.c $(LIB) $(MM) ../lib/new.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/test_cxx.o test_cxx.cpp
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/new.o ../lib/new.cpp
	$(CC) $(CFLAGS) -c -o $(BUILD)/stubs.o stubs.c
	$(CC) $(CFLAGS) -o $@ $(BUILD)/test_cxx.o $(BUILD)/new.o $(BUILD)/stubs.o $(filter %.c,$(LIB) $(MM)) -lstdc++
$(BUILD)/test_gfx: test_gfx.c stubs.c $(LIB) ../steel/canvas.c ../steel/pixdev.c \
	../steel/gfx_data.c $(THEME)
$(BUILD)/test_shell: test_shell.c stubs.c $(LIB) ../qubix/shlib.c

# the desktop and its programs, minus the qubix glue that needs a kernel
DESK	:= $(filter-out ../apps/desk_main.c,$(wildcard ../apps/*.c))
$(BUILD)/test_desk: test_desk.c stubs.c $(LIB) $(MM) $(DESK) $(CXXOBJ) ../steel/surface.c \
	../steel/canvas.c ../steel/pixdev.c ../steel/gfx_data.c ../steel/icon_data.c

# the scheduler test runs the real ctx_switch: build it as a 32-bit program
$(BUILD)/switch.o: ../arch/x86/switch.asm
	@mkdir -p $(BUILD)
	$(NASM) -f elf32 -dHOSTTEST $< -o $@

$(BUILD)/test_sched: test_sched.c stubs32.c $(LIB) $(MM) \
	../aether/sched.c ../aether/task.c ../aether/waitq.c \
	../aether/ipc.c ../aether/clock.c $(BUILD)/switch.o
	@mkdir -p $(BUILD)
	$(CC) -m32 -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
	    -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -no-pie \
	    -fno-tree-loop-distribute-patterns -fno-omit-frame-pointer \
	    -fcf-protection=none -nostdlib -static -DHIX_HOST_TEST -I../include \
	    -I. -o $@ $(filter %.c %.o,$^)

$(BUILD)/%:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(filter %.c %.o,$^)

clean:
	rm -rf $(BUILD)
