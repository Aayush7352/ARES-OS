# ARES OS, Architecture

ARES is a 64-bit hobby operating system built from scratch for the x86_64 platform. It draws clear inspiration from classic UNIX design: a monolithic kernel, a syscall trap, processes with priorities, a block-oriented file system, and a shell on top. The goal of this document is to give a single, honest map of how the pieces fit together, from the first byte the CPU executes to the moment the shell prompt appears.

## Boot Chain

The firmware loads sector zero of the boot disk into memory at `0x7C00` and jumps to it. That sector is **stage1**, our MBR. Stage1 has one job: load stage2 from disk and hand off to it. We keep stage1 tiny because we only get 512 bytes, minus the 64-byte partition table and the `0x55AA` signature.

Stage2 does the heavy lifting before the kernel can run safely:

1. Enable the **A20 line** so memory above 1 MB is addressable.
2. Build a flat **GDT** with code and data segments for 64-bit mode.
3. Set up identity-mapped page tables at `0x1000` (PML4), `0x2000` (PDPT), `0x3000` (PD).
4. Set `CR4.PAE`, write `CR3`, set `EFER.LME`, then `CR0.PG`. The CPU is now in long mode.
5. Long jump into 64-bit code and call the kernel entry point at `0x100000`.

```text
+-------------------+      +-------------------+      +-------------------+
|  BIOS / Firmware  | ---> |  Stage1 (MBR)     | ---> |  Stage2 (loader)  |
|                   |      |  @ 0x7C00, 512B   |      |  A20, GDT, paging |
+-------------------+      +-------------------+      +---------+---------+
                                                                |
                                                                v
                                                      +-------------------+
                                                      |  Kernel @ 0x100000|
                                                      |  _start in C      |
                                                      +-------------------+
```

## Memory Map

The kernel lives in a fixed, predictable layout. Nothing fancy, no relocation. This makes debugging in QEMU's monitor trivial.

```text
0x00000000 - 0x00000FFF   Real-mode IVT and BIOS scratch (unused after boot)
0x00001000 - 0x00003FFF   Boot-time page tables (PML4, PDPT, PD)
0x00007C00 - 0x00007DFF   Stage1 MBR (overwritten once stage2 is live)
0x00010000 - 0x0001FFFF   Stage2 loader
0x000B8000 - 0x000BFFFF   VGA text framebuffer (80x25 x 2 bytes)
0x00100000 - 0x00105FFF   Kernel .text + .rodata + .data
0x00106000 - 0x00109FFF   Kernel stack (16 KB, grows down from 0x10A000)
0x0010A000 - 0x0010FFFF   BSS, IDT (4 KB), PCB pool, bitmaps
0x00110000 - 0x03FFFFFF   PMM-managed heap and user pages (up to 64 MB)
```

The linker script anchors `.text` at `0x100000` and reserves the stack and BSS by symbol. The PMM treats everything from `0x110000` upward as a 4 KB page pool tracked by a single bitmap.

## Kernel Subsystems

Once `_start` runs, initialization happens in a fixed order. Each step depends on the one before it.

```c
typedef struct {
    uint64_t magic;
    uint64_t mem_lower_kb;
    uint64_t mem_upper_kb;
    uint16_t boot_drive;
} boot_info_t;

typedef enum {
    ARES_OK = 0,
    ARES_ENOMEM,
    ARES_EIO,
    ARES_ENOENT,
    ARES_EPERM,
    ARES_EINVAL,
} ares_status_t;
```

Init order:

1. `console_init` clears VGA, sets cursor.
2. `interrupt_init` builds the IDT, installs exception stubs.
3. `irq_init` remaps the PIC (master to `0x20`, slave to `0x28`), unmasks timer and keyboard.
4. `pmm_init` walks memory and marks pages free.
5. `vmm_init` switches to kernel page tables managed by the VMM.
6. `heap_init` carves a first-fit heap out of mapped pages.
7. `ata_init`, `fs_mount` bring storage up.
8. `process_init` reserves PCB slots, creates the idle task.
9. `scheduler_init` picks a policy (default Round Robin).
10. `keyboard_init`, `rtc_init`, `shell_start`.

After that, the kernel `sti`s and falls into the idle loop. Everything else happens from interrupts and from the shell process.

## Build Pipeline

The build is a flat Makefile. No autotools, no CMake.

```text
src/*.c, src/*.S
   |
   |  x86_64-elf-gcc -ffreestanding -mno-red-zone -Wall -Werror -c
   v
build/*.o
   |
   |  x86_64-elf-ld -T linker.ld
   v
build/kernel.elf
   |
   |  objcopy -O binary  +  cat stage1.bin stage2.bin kernel.bin
   v
ares.img  ---> qemu-system-x86_64 -drive file=ares.img,format=raw
```

`-Werror` is non-negotiable. The toolchain is cross-compiled because the host's libc and runtime would silently leak into the binary otherwise. There's a `make qemu` target for fast iteration and a `make debug` target that adds `-s -S` so GDB can attach.

## Why This Shape

Every choice here trades flexibility for clarity. Fixed addresses make crash dumps readable at a glance. A monolithic kernel keeps the call graph short. Initialization order is hand-written rather than discovered, so a regression in `pmm_init` can't silently break `fs_mount`. The kernel is meant to be read end to end by one person on one afternoon, and the architecture reflects that.
