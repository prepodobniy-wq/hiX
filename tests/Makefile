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
TESTS	:= test_lib test_mm test_ipc test_vfs test_steel test_shell test_gfx test_chess test_cxx test_gantry test_desk test_sdk
NASM	?= nasm

.PHONY: run clean
run: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for t in $(TESTS); do echo "== $$t"; $(BUILD)/$$t; done; \
	echo "== test_fastmem (32-bit, string instructions)"; \
	$(MAKE) --no-print-directory $(BUILD)/test_fastmem || exit 1; \
	$(BUILD)/test_fastmem || exit 1; \
	echo "== test_uspace (32-bit, ELF and address spaces)"; \
	$(MAKE) --no-print-directory $(BUILD)/test_uspace || exit 1; \
	$(BUILD)/test_uspace || exit 1; \
	echo "== test_sched (32-bit, real context switches)"; \
	$(MAKE) --no-print-directory $(BUILD)/test_sched || exit 1; \
	$(BUILD)/test_sched || exit 1; \
	echo; echo "all host tests passed"

$(BUILD)/test_lib: test_lib.c stubs.c $(LIB)
$(BUILD)/test_mm: test_mm.c stubs.c $(LIB) $(MM)
$(BUILD)/test_ipc: test_ipc.c stubs.c $(LIB) $(MM) ../aether/ipc.c
# the SDK programs are built with the real compiler, then checked
$(BUILD)/sdk/initfs.bin: $(wildcard ../user/apps/*.c ../user/sdk/*) ../scripts/mkapps.py
	@mkdir -p $(BUILD)/sdk
	python3 ../scripts/mkapps.py $(BUILD)/sdk $(CC) 2>&1 | grep -v "RWX permissions" || true

$(BUILD)/test_sdk: test_sdk.c stubs.c $(LIB) $(MM) ../qubix/vfs.c ../qubix/initfs.c ../lib/elf.c \
	$(BUILD)/sdk/initfs.bin

$(BUILD)/test_vfs: test_vfs.c stubs.c $(LIB) $(MM) ../qubix/vfs.c
$(BUILD)/test_steel: test_steel.c stubs.c $(LIB) $(MM) \
	../kernel/bootconfig.c ../steel/surface.c ../steel/device.c \
	../steel/compositor.c ../steel/console.c

# C++ parts are compiled to objects first, then linked into the C test
CXXOBJ	:= $(patsubst ../%.cpp,$(BUILD)/%.o,$(wildcard ../apps/*.cpp ../steel/*.cpp))
$(BUILD)/apps/%.o: ../apps/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(BUILD)/steel/%.o: ../steel/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
THEME	:= $(BUILD)/steel/theme.o $(BUILD)/steel/panel.o ../steel/icon_data.c

$(BUILD)/test_chess: test_chess.c stubs.c $(LIB) $(BUILD)/apps/chess_rules.o
$(BUILD)/test_cxx: test_cxx.cpp stubs.c $(LIB) $(MM) ../lib/new.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/test_cxx.o test_cxx.cpp
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/new.o ../lib/new.cpp
	$(CC) $(CFLAGS) -c -o $(BUILD)/stubs.o stubs.c
	$(CC) $(CFLAGS) -o $@ $(BUILD)/test_cxx.o $(BUILD)/new.o $(BUILD)/stubs.o $(filter %.c,$(LIB) $(MM)) -lstdc++
$(BUILD)/test_gantry: test_gantry.cpp stubs.c $(LIB) $(MM) ../lib/new.cpp \
	../gantry/registry.cpp ../gantry/drivers.cpp ../gantry/kit.cpp ../gantry/tweak.cpp ../gantry/tweaks/procwatch.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/test_gantry.o test_gantry.cpp
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/new.o ../lib/new.cpp
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/g_registry.o ../gantry/registry.cpp
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/g_drivers.o ../gantry/drivers.cpp
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/g_kit.o ../gantry/kit.cpp
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/g_tweak.o ../gantry/tweak.cpp
	$(CXX) $(CXXFLAGS) -c -o $(BUILD)/g_tweaks.o ../gantry/tweaks/procwatch.cpp
	$(CC) $(CFLAGS) -c -o $(BUILD)/stubs.o stubs.c
	$(CC) $(CFLAGS) -o $@ $(BUILD)/test_gantry.o $(BUILD)/new.o $(BUILD)/g_registry.o \
		$(BUILD)/g_drivers.o $(BUILD)/g_kit.o $(BUILD)/g_tweak.o $(BUILD)/g_tweaks.o $(BUILD)/stubs.o $(filter %.c,$(LIB) $(MM)) -lstdc++
$(BUILD)/test_gfx: test_gfx.c stubs.c $(LIB) ../steel/canvas.c ../steel/pixdev.c \
	../steel/gfx_data.c $(THEME)
$(BUILD)/test_shell: test_shell.c stubs.c $(LIB) ../qubix/shlib.c

# the desktop and its programs, minus the qubix glue that needs a kernel
DESK	:= $(filter-out ../apps/desk_main.c,$(wildcard ../apps/*.c)) ../lib/hixfetch.c
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

$(BUILD)/test_uspace: test_uspace.c stubs32.c $(LIB) ../lib/elf.c ../aether/uspace.c
	@mkdir -p $(BUILD)
	$(CC) -m32 -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
	    -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -no-pie \
	    -fno-tree-loop-distribute-patterns -fno-omit-frame-pointer \
	    -fcf-protection=none -nostdlib -static -DHIX_HOST_TEST -I../include \
	    -I. -o $@ $(filter %.c %.o,$^)

$(BUILD)/test_fastmem: test_fastmem.c ../lib/fastmem.h
	@mkdir -p $(BUILD)
	$(CC) -m32 -std=gnu11 -g -O2 -Wall -Wextra -ffreestanding -fno-builtin \
	    -fno-tree-loop-distribute-patterns -fno-stack-protector -fno-pie -no-pie \
	    -fcf-protection=none -nostdlib -static -I../include -I. -o $@ test_fastmem.c

$(BUILD)/%:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(filter %.c %.o,$^)

clean:
	rm -rf $(BUILD)
