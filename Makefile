#==============================================================================
# ARES OS - Build System
#==============================================================================
# Targets:
#   all      - Build the complete OS disk image
#   run      - Build and run in QEMU
#   debug    - Build and run with GDB debug support
#   clean    - Remove all build artifacts
#   distclean - Remove all build artifacts and disk image
#
# Toolchain:
#   CC    = x86_64-elf-gcc (cross-compiler)
#   AS    = x86_64-elf-as   (cross-assembler)
#   LD    = x86_64-elf-ld   (cross-linker)
#   OBJCP = x86_64-elf-objcopy
#   NASM  = nasm
#   QEMU  = qemu-system-x86_64
#   GDB   = x86_64-elf-gdb
#==============================================================================

#==============================================================================
# Project Configuration
#==============================================================================
PROJECT   := ARES OS
VERSION   := 0.1.0
ARCH      := x86_64

#==============================================================================
# Directory Structure
#==============================================================================
BUILD_DIR    := build
BOOT_DIR     := boot
KERNEL_DIR   := kernel
ARCH_DIR     := $(KERNEL_DIR)/arch/$(ARCH)
DRIVERS_DIR  := drivers
LIB_DIR      := lib
MEMORY_DIR   := memory
PROCESS_DIR  := process
SCHED_DIR    := scheduler
FS_DIR       := fs
SHELL_DIR    := shell
SYSCALL_DIR  := syscall
SECURITY_DIR := security
EDITOR_DIR   := editor
CALC_DIR     := calc
TEST_DIR     := test
OBSERV_DIR   := observability
BENCH_DIR    := benchmark
DOCS_DIR     := docs

#==============================================================================
# Toolchain
#==============================================================================
CC      := x86_64-elf-gcc
AS      := x86_64-elf-as
LD      := x86_64-elf-ld
NASM    := nasm
OBJCP   := x86_64-elf-objcopy
QEMU    := qemu-system-x86_64
GDB     := x86_64-elf-gdb

#==============================================================================
# Flags
#==============================================================================
# C flags: freestanding, no standard lib, optimizations, warnings
CFLAGS  := -ffreestanding -nostdlib -nostartfiles -nodefaultlibs
CFLAGS  += -Wall -Wextra -Werror -Wpedantic
CFLAGS  += -Wshadow -Wmissing-prototypes -Wstrict-prototypes
CFLAGS  += -Wconversion -Wsign-conversion -Wcast-align
CFLAGS  += -O2 -fno-stack-protector -fno-pic -mno-red-zone
CFLAGS  += -mcmodel=large -mno-mmx -mno-sse -mno-sse2
CFLAGS  += -I$(KERNEL_DIR) -I$(LIB_DIR) -I$(DRIVERS_DIR)
CFLAGS  += -I$(MEMORY_DIR) -I$(PROCESS_DIR) -I$(SCHED_DIR)
CFLAGS  += -I$(FS_DIR) -I$(SHELL_DIR) -I$(SYSCALL_DIR) -I$(SECURITY_DIR)
CFLAGS  += -I$(EDITOR_DIR) -I$(CALC_DIR) -I$(TEST_DIR) -I$(OBSERV_DIR)
CFLAGS  += -I$(BENCH_DIR)
CFLAGS  += -DARES_ARCH_$(ARCH)
CFLAGS  += -std=c99
CFLAGS  += -MMD -MP                  # Dependency tracking

# Assembly flags (GAS, for .S files)
ASFLAGS := -I$(KERNEL_DIR)

# NASM flags (for .asm boot files)
NASMFLAGS := -f bin

# Linker flags
LDFLAGS := -nostdlib
LDFLAGS += -T link.ld -Map $(BUILD_DIR)/aresos.map

#==============================================================================
# Source Files
#==============================================================================

# Architecture-specific assembly (NASM)
ARCH_ASM_SRCS := $(wildcard $(ARCH_DIR)/*.asm)
ARCH_ASM_OBJS := $(patsubst $(ARCH_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ARCH_ASM_SRCS))

# Kernel C sources
KERNEL_C_SRCS := $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_C_OBJS := $(patsubst $(KERNEL_DIR)/%.c, $(BUILD_DIR)/%.o, $(KERNEL_C_SRCS))

# Process C sources
PROCESS_C_SRCS := $(wildcard $(PROCESS_DIR)/*.c)
PROCESS_C_OBJS := $(patsubst $(PROCESS_DIR)/%.c, $(BUILD_DIR)/%.o, $(PROCESS_C_SRCS))

# Process assembly sources (NASM)
PROCESS_ASM_SRCS := $(wildcard $(PROCESS_DIR)/*.asm)
PROCESS_ASM_OBJS := $(patsubst $(PROCESS_DIR)/%.asm, $(BUILD_DIR)/%.o, $(PROCESS_ASM_SRCS))

# Scheduler C sources (exclude the old benchmark.c — superseded by benchmark/)
SCHED_C_SRCS := $(filter-out $(SCHED_DIR)/benchmark.c, $(wildcard $(SCHED_DIR)/*.c))
SCHED_C_OBJS := $(patsubst $(SCHED_DIR)/%.c, $(BUILD_DIR)/%.o, $(SCHED_C_SRCS))

# Memory C sources
MEMORY_C_SRCS := $(wildcard $(MEMORY_DIR)/*.c)
MEMORY_C_OBJS := $(patsubst $(MEMORY_DIR)/%.c, $(BUILD_DIR)/%.o, $(MEMORY_C_SRCS))

# Drivers C sources
DRIVERS_C_SRCS := $(wildcard $(DRIVERS_DIR)/*.c)
DRIVERS_C_OBJS := $(patsubst $(DRIVERS_DIR)/%.c, $(BUILD_DIR)/%.o, $(DRIVERS_C_SRCS))

# Filesystem C sources
FS_C_SRCS := $(wildcard $(FS_DIR)/*.c)
FS_C_OBJS := $(patsubst $(FS_DIR)/%.c, $(BUILD_DIR)/%.o, $(FS_C_SRCS))

# Syscall C sources
SYSCALL_C_SRCS := $(wildcard $(SYSCALL_DIR)/*.c)
SYSCALL_C_OBJS := $(patsubst $(SYSCALL_DIR)/%.c, $(BUILD_DIR)/%.o, $(SYSCALL_C_SRCS))

# Shell C sources
SHELL_C_SRCS := $(wildcard $(SHELL_DIR)/*.c)
SHELL_C_OBJS := $(patsubst $(SHELL_DIR)/%.c, $(BUILD_DIR)/%.o, $(SHELL_C_SRCS))

# Security C sources
SECURITY_C_SRCS := $(wildcard $(SECURITY_DIR)/*.c)
SECURITY_C_OBJS := $(patsubst $(SECURITY_DIR)/%.c, $(BUILD_DIR)/%.o, $(SECURITY_C_SRCS))

# Editor C sources
EDITOR_C_SRCS := $(wildcard $(EDITOR_DIR)/*.c)
EDITOR_C_OBJS := $(patsubst $(EDITOR_DIR)/%.c, $(BUILD_DIR)/%.o, $(EDITOR_C_SRCS))

# Calculator C sources
CALC_C_SRCS := $(wildcard $(CALC_DIR)/*.c)
CALC_C_OBJS := $(patsubst $(CALC_DIR)/%.c, $(BUILD_DIR)/%.o, $(CALC_C_SRCS))

# Test C sources
TEST_C_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_C_OBJS := $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/%.o, $(TEST_C_SRCS))

# Observability C sources
OBSERV_C_SRCS := $(wildcard $(OBSERV_DIR)/*.c)
OBSERV_C_OBJS := $(patsubst $(OBSERV_DIR)/%.c, $(BUILD_DIR)/%.o, $(OBSERV_C_SRCS))

# Benchmark C sources
BENCH_C_SRCS := $(wildcard $(BENCH_DIR)/*.c)
BENCH_C_OBJS := $(patsubst $(BENCH_DIR)/%.c, $(BUILD_DIR)/%.o, $(BENCH_C_SRCS))

# Aggregate kernel objects
KERNEL_OBJS   := $(ARCH_ASM_OBJS) $(PROCESS_ASM_OBJS) $(KERNEL_C_OBJS) $(PROCESS_C_OBJS) $(MEMORY_C_OBJS) $(SCHED_C_OBJS) $(DRIVERS_C_OBJS) $(FS_C_OBJS) $(SYSCALL_C_OBJS) $(SHELL_C_OBJS) $(SECURITY_C_OBJS) $(EDITOR_C_OBJS) $(CALC_C_OBJS) $(TEST_C_OBJS) $(OBSERV_C_OBJS) $(BENCH_C_OBJS)

# Bootloader binaries
STAGE1_BIN    := $(BUILD_DIR)/stage1.bin
STAGE2_BIN    := $(BUILD_DIR)/stage2.bin
KERNEL_ELF    := $(BUILD_DIR)/aresos.elf
KERNEL_BIN    := $(BUILD_DIR)/aresos.bin
DISK_IMG      := $(BUILD_DIR)/aresos.img

#==============================================================================
# Default Target
#==============================================================================
.PHONY: all
all: $(DISK_IMG)

#==============================================================================
# Build Rules
#==============================================================================

# Stage 1 bootloader (MBR, flat binary)
$(STAGE1_BIN): $(BOOT_DIR)/stage1.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

# Stage 2 bootloader (flat binary)
$(STAGE2_BIN): $(BOOT_DIR)/stage2.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

# Architecture assembly (NASM, ELF64 objects)
$(BUILD_DIR)/%.o: $(ARCH_DIR)/%.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 -o $@ $<

# C source files (kernel directory)
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (process directory)
$(PROCESS_C_OBJS): $(BUILD_DIR)/%.o: $(PROCESS_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Assembly source files (process directory, NASM ELF64)
$(PROCESS_ASM_OBJS): $(BUILD_DIR)/%.o: $(PROCESS_DIR)/%.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 -o $@ $<

# C source files (scheduler directory)
$(SCHED_C_OBJS): $(BUILD_DIR)/%.o: $(SCHED_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (memory directory)
$(MEMORY_C_OBJS): $(BUILD_DIR)/%.o: $(MEMORY_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (drivers directory)
$(DRIVERS_C_OBJS): $(BUILD_DIR)/%.o: $(DRIVERS_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (fs directory)
$(FS_C_OBJS): $(BUILD_DIR)/%.o: $(FS_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (syscall directory)
$(SYSCALL_C_OBJS): $(BUILD_DIR)/%.o: $(SYSCALL_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (shell directory)
$(SHELL_C_OBJS): $(BUILD_DIR)/%.o: $(SHELL_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (security directory)
$(SECURITY_C_OBJS): $(BUILD_DIR)/%.o: $(SECURITY_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (editor directory)
$(EDITOR_C_OBJS): $(BUILD_DIR)/%.o: $(EDITOR_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (calculator directory) — needs SSE for double arithmetic
CALC_CFLAGS := $(filter-out -mno-mmx -mno-sse -mno-sse2, $(CFLAGS)) -msse -msse2 -mfpmath=sse
$(CALC_C_OBJS): $(BUILD_DIR)/%.o: $(CALC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CALC_CFLAGS) -c -o $@ $<

# C source files (test directory)
$(TEST_C_OBJS): $(BUILD_DIR)/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (observability directory)
$(OBSERV_C_OBJS): $(BUILD_DIR)/%.o: $(OBSERV_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# C source files (benchmark directory)
$(BENCH_C_OBJS): $(BUILD_DIR)/%.o: $(BENCH_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Link kernel ELF
$(KERNEL_ELF): $(KERNEL_OBJS) link.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

# Convert ELF to flat binary
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCP) -O binary $< $@

# Create disk image
$(DISK_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	@echo "Creating disk image..."
	# Create a 16MB disk image filled with zeros
	dd if=/dev/zero bs=1M count=16 of=$@ 2>/dev/null
	# Write Stage 1 (MBR) to sector 0
	dd if=$(STAGE1_BIN) of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	# Write Stage 2 to sectors 1-64 (pad to 32KB)
	dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	# Write kernel to sectors 65+ (pad to 256KB)
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=65 conv=notrunc 2>/dev/null
	@echo "Disk image created: $@ ($(shell ls -lh $@ | awk '{print $$5}'))"

#==============================================================================
# QEMU Targets
#==============================================================================

.PHONY: run
run: $(DISK_IMG)
	$(QEMU) -drive format=raw,file=$(DISK_IMG) \
		-m 256M \
		-smp 2 \
		-serial stdio \
		-display sdl \
		-cpu max

.PHONY: run-nographic
run-nographic: $(DISK_IMG)
	$(QEMU) -drive format=raw,file=$(DISK_IMG) \
		-m 256M \
		-smp 2 \
		-nographic \
		-cpu max

.PHONY: run-sdl
run-sdl: $(DISK_IMG)
	$(QEMU) -drive format=raw,file=$(DISK_IMG) \
		-m 256M \
		-smp 2 \
		-display sdl \
		-cpu max

#==============================================================================
# Debug Target (QEMU + GDB)
#==============================================================================

.PHONY: debug
debug: $(DISK_IMG)
	@echo "Starting QEMU with GDB server on port 1234..."
	$(QEMU) -drive format=raw,file=$(DISK_IMG) \
		-m 256M \
		-smp 1 \
		-s -S \
		-display sdl \
		-cpu max \
		-d int,cpu_reset \
		&
	@sleep 1
	@echo "Starting GDB..."
	$(GDB) -ex "target remote localhost:1234" \
		-ex "symbol-file $(KERNEL_ELF)" \
		-ex "set architecture i386:x86-64" \
		-ex "break kernel_main" \
		-ex "continue"

#==============================================================================
# Cleanup
#==============================================================================

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/*.o $(BUILD_DIR)/*.d $(BUILD_DIR)/*.bin
	rm -rf $(BUILD_DIR)/*.elf $(BUILD_DIR)/*.map
	@echo "Clean complete."

.PHONY: distclean
distclean: clean
	rm -f $(DISK_IMG)
	@echo "Distclean complete."

#==============================================================================
# Utility Targets
#==============================================================================

.PHONY: size
size: $(KERNEL_ELF)
	@echo "Kernel sections:"
	$(OBJCP) -I elf64-x86-64 -O binary /dev/null /dev/null 2>/dev/null
	x86_64-elf-size $(KERNEL_ELF)
	@echo ""
	@echo "Binary sizes:"
	ls -lh $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN) 2>/dev/null

.PHONY: objdump
objdump: $(KERNEL_ELF)
	x86_64-elf-objdump -d $(KERNEL_ELF) | head -100

.PHONY: hexdump
hexdump: $(KERNEL_BIN)
	xxd -l 128 $(KERNEL_BIN)

# Include auto-generated dependency files
-include $(BUILD_DIR)/*.d
