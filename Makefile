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
#   make shots        render the desktop frames to build/shots/*.png
#   make shots-check  compare the frames with tools/shotsum/golden.txt (Rust)
#   make strict       build with -Werror
#   make rust-sysroot fetch the i386 Rust core on Debian/Ubuntu (no root)
#   make rust         build rust/hix-core and link it into the kernel
#   make rust-test    host unit tests for the Rust crate (cargo)
#   make crypto-test  Python-generated vectors vs the Rust C ABI
#   make config       show config/hix.conf (splash, quiet, screensaver)
#   make version      print the release version from include/hix/version.h
#   make verify       host tests, style, multiboot, Rust, crypto, frames
#   make smoke        boot in QEMU and check the shell over COM1
#   make release      strict Rust build + verify + smoke + release package
#   make help         this list
#
# Boot experience: config/hix.conf, scripts/hixctl (bash), docs/BOOT.md.
# Variables: V=1 verbose, CROSS_COMPILE=i686-elf- to use a cross toolchain,
#            RUST=auto|1|0 (default auto), RUST_SYSROOT=/path/to/sysroot.

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
	   $(WARN) -Iinclude -I$(BUILD) -MMD -MP

# C++ is for the layers above the kernel proper (hix itself stays C): no
# exceptions, no RTTI, no standard library, so it needs nothing but lib/cxxrt.c
CXXFLAGS := -m32 -march=i686 -std=gnu++17 -O2 -g \
	   -ffreestanding -fno-builtin -nostdinc++ \
	   -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
	   -fno-stack-protector -fno-pic -fno-pie -fcf-protection=none \
	   -fno-asynchronous-unwind-tables -fno-omit-frame-pointer \
	   -fno-tree-loop-distribute-patterns \
	   -mno-sse -mno-mmx -mno-sse2 -mno-80387 -mno-red-zone \
	   $(WARN) -Iinclude -I$(BUILD) -MMD -MP

NASMFLAGS := -f elf32 -g -F dwarf -I$(BUILD)/user/

LDFLAGS	:= -m elf_i386 -T linker.ld -nostdlib -z noexecstack \
	   --build-id=none --gc-sections \
	   $(shell $(LD) --help 2>/dev/null | grep -q no-warn-rwx && echo --no-warn-rwx-segments)

# --- Rust in the kernel -------------------------------------------------
#
# The module is optional: a machine without an i686 Rust core still builds a
# kernel, but SYS_crypto returns -ENOSYS. RUST=1 makes a missing toolchain a
# hard error; `make rust-sysroot` prepares a local one on Debian/Ubuntu.

RUST		?= auto
RUST_SYSROOT	?=
export RUST_SYSROOT

ifeq ($(RUST),0)
RUST_AVAILABLE	:=
else
RUST_AVAILABLE	:= $(shell scripts/rust-available.sh >/dev/null 2>&1 && echo 1)
endif

ifeq ($(RUST),1)
ifeq ($(RUST_AVAILABLE),)
$(error Rust requested, but no i686 core is available; run 'make rust-sysroot' or set RUST_SYSROOT)
endif
endif

ifeq ($(RUST_AVAILABLE),1)
CFLAGS		+= -DHIX_HAVE_RUST=1
CXXFLAGS	+= -DHIX_HAVE_RUST=1
RUST_ARCHIVE	:= $(BUILD)/rust/libhix_rust.a
endif

# --- build-time configuration ------------------------------------------
#
# config/hix.conf is the single place for the boot experience: splash, quiet
# boot, screensaver. scripts/hixctl validates it and writes the C header that
# the kernel includes as <hixconfig.h>. "make" and "scripts/hixctl build" do
# the same thing; the script only adds the friendly front end.

CONFIG		:= config/hix.conf
CONFIG_H	:= $(BUILD)/hixconfig.h

$(CONFIG_H): $(CONFIG) scripts/hixctl
	@echo "  CONF    $@"
	$(Q)scripts/hixctl gen-config $(CONFIG) $@

# --- sources ------------------------------------------------------------

C_SRCS	:= $(wildcard arch/x86/*.c kernel/*.c aether/*.c qubix/*.c \
		      steel/*.c drivers/*.c lib/*.c apps/*.c)
CXX_SRCS := $(wildcard apps/*.cpp lib/*.cpp steel/*.cpp qubix/*.cpp gantry/*.cpp gantry/tweaks/*.cpp)
ASM_SRCS := $(wildcard arch/x86/*.asm)
OBJS	:= $(addprefix $(BUILD)/,$(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o) \
			 $(ASM_SRCS:.asm=.asm.o))
DEPS	:= $(OBJS:.o=.d)
OBJS	+= $(RUST_ARCHIVE)

# RUST=0/1 changes HIX_HAVE_RUST in CFLAGS, which make does not track. Keep a
# stamp so qubix/crypto.o is rebuilt when the Rust mode flips.
RUST_STATE	:= $(BUILD)/rust-state
RUST_MODE	:= $(if $(RUST_AVAILABLE),1,0)
$(shell mkdir -p $(BUILD); \
	if [ ! -f $(RUST_STATE) ] || \
	   [ "$$(cat $(RUST_STATE) 2>/dev/null)" != "$(RUST_MODE)" ]; then \
		echo $(RUST_MODE) > $(RUST_STATE); \
	fi)

# --- targets ------------------------------------------------------------

.PHONY: all kernel iso run run-iso run-serial debug test check strict shots \
	clean distclean help shots shots-check shots-golden config version \
	verify smoke release rust rust-test rust-sysroot crypto-test
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

$(BUILD)/%.o: %.c | $(CONFIG_H)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# HIX_HAVE_RUST is a CFLAGS define, invisible to make's dependency graph.
$(BUILD)/qubix/crypto.o: $(RUST_STATE)

$(BUILD)/%.o: %.cpp | $(CONFIG_H)
	@echo "  CXX     $<"
	@mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: %.asm
	@echo "  NASM    $<"
	@mkdir -p $(dir $@)
	$(Q)$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD)/rust/libhix_rust.a: rust/hix-core/src/lib.rs \
		$(wildcard rust/hix-core/src/*.rs) rust/hix-core/Cargo.toml \
		scripts/rust-common.sh scripts/rust-build.sh
	@echo "  RUST    $@"
	@mkdir -p $(dir $@)
	$(Q)scripts/rust-build.sh $@

# ring 3 demo programs: flat ELF images built by nasm, embedded by ublob.asm
USER_ELFS := $(addprefix $(BUILD)/user/,hello.elf fault.elf probe.elf)

$(BUILD)/user/%.elf: user/%.asm user/elfhdr.inc
	@echo "  NASM    $<"
	@mkdir -p $(dir $@)
	$(Q)$(NASM) -f bin -I. $< -o $@

# SDK programs (user/apps) and the initfs archive that is unpacked into /bin
$(BUILD)/user/initfs.bin: $(wildcard user/apps/*.c user/sdk/*) scripts/mkapps.py \
		include/hix/kitabi.h include/hix/cryptoabi.h
	@echo "  MKAPPS  $@"
	@mkdir -p $(dir $@)
	$(Q)python3 scripts/mkapps.py $(BUILD)/user $(CC) 2>&1 | grep -v "RWX permissions" || true
	$(Q)test -s $@

$(BUILD)/arch/x86/ublob.asm.o: $(USER_ELFS) $(BUILD)/user/initfs.bin

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

config:
	@scripts/hixctl show

version:
	@echo $(VERSION)

# Sequential on purpose: several of these targets share tests/build and
# build/ppm, and `make -j verify` must not race them.
verify:
	$(MAKE) --no-print-directory check
	$(MAKE) --no-print-directory test
	$(MAKE) --no-print-directory rust-test
	$(MAKE) --no-print-directory crypto-test
	$(MAKE) --no-print-directory shots-check
	@echo "verify: all checks passed"

smoke: kernel
	@python3 scripts/smoke.py

release:
	@scripts/release.sh

shots:
	@python3 scripts/shots.py

check: kernel
	@scripts/check-style.sh
	@python3 scripts/check-multiboot.py $(BUILD)/hix.elf

# The Rust crate has its own host tests (x86_64, no toolchain target needed).
rust-test:
	cargo test --offline --manifest-path rust/hix-core/Cargo.toml --lib

rust-sysroot:
	@scripts/get-rust-i386.sh

rust:
	@rm -f $(BUILD)/rust/libhix_rust.a
	@$(MAKE) --no-print-directory RUST=1 $(BUILD)/rust/libhix_rust.a

# Python generates random vectors, then compiles the Rust C ABI on the host.
crypto-test:
	python3 scripts/crypto_check.py

# The Rust tool in tools/shotsum hashes the rendered frames. Needs cargo.
PPMDIR := $(BUILD)/ppm
define render_ppm
	$(Q)rm -rf $(PPMDIR) && mkdir -p $(PPMDIR)
	$(Q)$(MAKE) --no-print-directory -C tests build/test_desk >/dev/null
	$(Q)DESK_SHOTS=$(abspath $(PPMDIR)) tests/build/test_desk >/dev/null
endef

shots-check:
	$(render_ppm)
	$(Q)cargo run -q --release --offline --manifest-path tools/shotsum/Cargo.toml -- \
		$(PPMDIR) --check tools/shotsum/golden.txt

shots-golden:
	$(render_ppm)
	$(Q)cargo run -q --release --offline --manifest-path tools/shotsum/Cargo.toml -- \
		$(PPMDIR) --write tools/shotsum/golden.txt

clean:
	rm -rf $(BUILD) tools/shotsum/target rust/hix-core/target
	$(MAKE) -C tests clean

distclean: clean

help:
	@sed -n '2,/^$$/p' Makefile | sed 's/^# \{0,1\}//'

-include $(DEPS)
