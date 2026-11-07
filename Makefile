# SPDX-License-Identifier: GPL-2.0-only
#
# hix - hi is not unix. A hybrid kernel for i386.
#
#   make              build build/hix.elf and, if GRUB is present, build/hix.iso
#   make run          boot the kernel in QEMU (no GRUB needed: -kernel)
#   make run-iso      boot the GRUB rescue ISO in QEMU
#   make run-serial   like run, with the serial console on your terminal
#   make test         host-side unit tests (pmm, heap, ipc, vfs, steel, ...)
#   make check        style check + Multiboot header check
#   make shots        render the desktop (host test frames) to PNG in build/shots
#   make strict       build with -Werror
#   make help         this list
#
# Variables: V=1 verbose, CROSS_COMPILE=i686-elf- to use a cross toolchain.

CROSS_COMPILE ?=
CC	:= $(CROSS_COMPILE)gcc
CXX	:= $(CROSS_COMPILE)g++
LD	:= $(CROSS_COMPILE)ld
NASM	:= nasm
QEMU	?= qemu-system-i386
BUILD	:= build

ifeq ($(V),1)
Q :=
else
Q := @
endif

VERSION := $(shell sed -n 's/^\#define HIX_RELEASE[[:space:]]*"\(.*\)"/\1/p' include/hix/version.h)

# --- flags --------------------------------------------------------------

WARN	:= -Wall -Wextra -Wshadow -Wno-unused-parameter
ifeq ($(STRICT),1)
WARN	+= -Werror
endif

CFLAGS	:= -m32 -march=i686 -std=gnu11 -O2 -g \
	   -ffreestanding -fno-builtin -nostdinc -isystem $(shell $(CC) -print-file-name=include) \
	   -fno-stack-protector -fno-pic -fno-pie -fcf-protection=none \
	   -fno-asynchronous-unwind-tables -fno-omit-frame-pointer \
	   -fno-tree-loop-distribute-patterns -fno-common \
	   -mno-sse -mno-mmx -mno-sse2 -mno-80387 -mno-red-zone \
	   $(WARN) -Iinclude -MMD -MP

# C++ is for the layers above the kernel proper (hix itself stays C): no
# exceptions, no RTTI, no standard library, so it needs nothing but lib/cxxrt.c
CXXFLAGS := -m32 -march=i686 -std=gnu++17 -O2 -g \
	   -ffreestanding -fno-builtin -nostdinc++ \
	   -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
	   -fno-stack-protector -fno-pic -fno-pie -fcf-protection=none \
	   -fno-asynchronous-unwind-tables -fno-omit-frame-pointer \
	   -fno-tree-loop-distribute-patterns \
	   -mno-sse -mno-mmx -mno-sse2 -mno-80387 -mno-red-zone \
	   $(WARN) -Iinclude -MMD -MP

NASMFLAGS := -f elf32 -g -F dwarf

LDFLAGS	:= -m elf_i386 -T linker.ld -nostdlib -z noexecstack \
	   --build-id=none \
	   $(shell $(LD) --help 2>/dev/null | grep -q no-warn-rwx && echo --no-warn-rwx-segments)

# --- sources ------------------------------------------------------------

C_SRCS	:= $(wildcard arch/x86/*.c kernel/*.c aether/*.c qubix/*.c \
		      steel/*.c drivers/*.c lib/*.c apps/*.c)
CXX_SRCS := $(wildcard apps/*.cpp lib/*.cpp steel/*.cpp qubix/*.cpp)
ASM_SRCS := $(wildcard arch/x86/*.asm)
OBJS	:= $(addprefix $(BUILD)/,$(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o) \
			 $(ASM_SRCS:.asm=.asm.o))
DEPS	:= $(OBJS:.o=.d)

# --- targets ------------------------------------------------------------

.PHONY: all kernel iso run run-iso run-serial debug test check strict shots \
	clean distclean help
.DEFAULT_GOAL := all

all: kernel
	$(Q)if command -v grub-mkrescue >/dev/null 2>&1 || \
	      command -v grub2-mkrescue >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory iso; \
	else \
		echo "  (grub-mkrescue not found: skipping the ISO, use 'make run')"; \
	fi

kernel: $(BUILD)/hix.elf

$(BUILD)/hix.elf: $(OBJS) linker.ld
	@echo "  LD      $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJS)
	$(Q)$(CROSS_COMPILE)size $@ 2>/dev/null || true

$(BUILD)/%.o: %.c
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -DHIX_VERSION=\"$(VERSION)\" -c $< -o $@

$(BUILD)/%.o: %.cpp
	@echo "  CXX     $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) -DHIX_VERSION=\"$(VERSION)\" -c $< -o $@

$(BUILD)/%.asm.o: %.asm
	@echo "  NASM    $<"
	@mkdir -p $(dir $@)
	$(Q)$(NASM) $(NASMFLAGS) $< -o $@

iso: $(BUILD)/hix.iso

$(BUILD)/hix.iso: $(BUILD)/hix.elf iso/boot/grub/grub.cfg
	@echo "  ISO     $@"
	$(Q)rm -rf $(BUILD)/isoroot && mkdir -p $(BUILD)/isoroot/boot/grub
	$(Q)cp $(BUILD)/hix.elf $(BUILD)/isoroot/boot/hix.elf
	$(Q)cp iso/boot/grub/grub.cfg $(BUILD)/isoroot/boot/grub/grub.cfg
	$(Q)(grub-mkrescue -o $@ $(BUILD)/isoroot || \
	     grub2-mkrescue -o $@ $(BUILD)/isoroot) >/dev/null 2>&1

QEMU_FLAGS := -m 128M

run: kernel
	$(QEMU) $(QEMU_FLAGS) -kernel $(BUILD)/hix.elf

run-serial: kernel
	$(QEMU) $(QEMU_FLAGS) -kernel $(BUILD)/hix.elf -serial stdio

run-iso: iso
	$(QEMU) $(QEMU_FLAGS) -cdrom $(BUILD)/hix.iso

debug: kernel
	$(QEMU) $(QEMU_FLAGS) -kernel $(BUILD)/hix.elf -serial stdio -s -S

strict:
	$(MAKE) clean
	$(MAKE) STRICT=1 kernel

test:
	$(MAKE) -C tests run

shots:
	@python3 scripts/shots.py

check: kernel
	@scripts/check-style.sh
	@python3 scripts/check-multiboot.py $(BUILD)/hix.elf

clean:
	rm -rf $(BUILD)
	$(MAKE) -C tests clean

distclean: clean

help:
	@sed -n '2,15p' Makefile | sed 's/^# \{0,1\}//'

-include $(DEPS)
