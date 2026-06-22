# ARES OS

> A UNIX-inspired x86_64 operating system from scratch.
> **ARES** = **A**dvanced **R**eliable **E**xtensible **S**ystem

![C](https://img.shields.io/badge/language-C-blue?style=flat-square)
![NASM](https://img.shields.io/badge/assembly-NASM-orange?style=flat-square)
![x86_64](https://img.shields.io/badge/arch-x86__64-red?style=flat-square)
![Status](https://img.shields.io/badge/status-developing-yellow?style=flat-square)
![Build](https://img.shields.io/badge/build-Makefile-green?style=flat-square)
![QEMU](https://img.shields.io/badge/tested-QEMU%2011.0-blueviolet?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-brightgreen?style=flat-square)

ARES is a hobby kernel written in C and NASM. It boots on bare x86_64, brings up long mode by hand, and runs a small UNIX-flavored userland on top: a shell, a text editor, a calculator, a filesystem, preemptive multitasking, and a handful of system services. No frameworks, no shortcuts. Just a Makefile, a linker script, and a lot of hex.

---

## Quick Start

**Prerequisites**

* `x86_64-elf-gcc` (cross compiler)
* `nasm` (assembler)
* `qemu-system-x86_64` (tested on QEMU 11.0)
* GNU `make`

**Build and run**

```bash
git clone <your-repo-url> ares-os
cd ares-os/OS
make all
make run
```

The kernel boots into the ARES shell. Type `help` to see what's available.

---

## Architecture

Boot flow, end to end:

```
BIOS  ->  Stage1 (MBR, 512B)  ->  Stage2 (A20 + GDT + Long Mode)  ->  Kernel Entry  ->  kernel_main()
```

Stage1 lives in the MBR. It loads Stage2 off disk. Stage2 enables the A20 line, sets up a GDT, builds 4-level page tables, flips the CPU into long mode, then jumps to the 64-bit kernel entry point. `kernel_main()` takes it from there: IDT, PIC remap, timer, keyboard, PMM, scheduler, shell.

---

## Subsystems

| Subsystem | Description | Status |
|-----------|-------------|--------|
| Bootloader | 2-stage: MBR loader + long-mode entry (A20, GDT, paging) | Done |
| Console | VGA text mode 80x25, color attributes, `printf` family | Done |
| Serial | 16550 UART for logging and host debugging | Done |
| IDT / ISR | 256-entry IDT, CPU exceptions, IRQ stubs in NASM | Done |
| PIC | 8259 remap to 0x20/0x28, EOI handling | Done |
| Timer | PIT @ 100 Hz, drives scheduler ticks and uptime | Done |
| Keyboard | PS/2 scancode set 1, ring buffer, line editing | Done |
| PMM | Bitmap physical memory manager, 60 MB pool | Done |
| Paging | 4-level page tables, identity + kernel mappings | Done |
| Heap | Simple `kmalloc` / `kfree`, free-list allocator | Done |
| Process | PCB, fork-style task creation, kernel threads | Done |
| Scheduler | Preemptive round-robin, timer-driven context switch | Done |
| Syscalls | `int 0x80` dispatch table, userland entry points | Done |
| Filesystem | In-memory FS, files + directories, read/write/create | Done |
| Shell | Line editor, builtins, command parsing, history | Done |
| Editor | Full-screen text editor with cursor and save | Done |
| Calculator | Expression parser, +-*/ with precedence | Done |
| Users | Multi-user login, password hashing, session state | Done |
| Logging | Ring-buffer kernel log, queryable via `log` | Done |
| Observability | `top`, `ps`, `meminfo`, `uptime`, perf counters | Done |
| Benchmark | Timed micro-benchmarks for memory and scheduler | Done |
| Tests | Self-test suite invoked by `test` builtin | Done |

---

## Memory Map

```
0x000000 - 0x003FFF   Page tables (PML4, PDPT, PD, PT)
0x100000 - 0x10FFFF   Kernel image (.text / .rodata / .data / .bss)
0x106000 - 0x10BFFF   Kernel stack, IDT, ISR handlers (>64 KB)
0x400000 - 0x4000000  PMM-managed physical memory (~60 MB pool)
```

The kernel is loaded at the conventional 1 MB mark. Page tables sit low. The PMM owns everything from 4 MB up.

---

## Shell Commands

| Command | What it does |
|---------|--------------|
| `help` | List all builtins |
| `echo <text>` | Print text to console |
| `clear` | Clear the screen |
| `ps` | List running processes |
| `top` | Live process and memory view |
| `date` | Show current uptime-derived date |
| `uptime` | Time since boot |
| `meminfo` | PMM stats, free / used pages |
| `reboot` | Triple-fault style reboot |
| `shutdown` | ACPI / QEMU shutdown |
| `editor <file>` | Open the text editor |
| `calc <expr>` | Evaluate an arithmetic expression |
| `test` | Run the kernel self-test suite |
| `bench` | Run micro-benchmarks |
| `users` | List registered users |
| `login` | Switch user session |
| `log` | Dump the kernel ring log |
| `cat <file>` | Print file contents |
| `ls` | List filesystem entries |
| `echo-to-file <file> <text>` | Write text to a file |

---

## Build System

| Target | Action |
|--------|--------|
| `make all` | Build bootloader, kernel, and disk image |
| `make boot` | Assemble Stage1 + Stage2 only |
| `make kernel` | Compile and link the kernel ELF |
| `make image` | Pack boot + kernel into a flat disk image |
| `make run` | Boot the image in QEMU |
| `make debug` | Run QEMU with `-s -S` for GDB attach |
| `make serial` | Run with serial output piped to `serial.log` |
| `make clean` | Remove `build/` and intermediate artifacts |
| `make iso` | Build a bootable ISO (where supported) |

---

## Testing

Two paths: an in-kernel self-test suite, and timed benchmarks.

**Self-tests** (`test` builtin):

* PMM allocate / free correctness
* Heap stress: alloc, free, fragmentation
* Scheduler fairness across N tasks
* Filesystem create / read / write / delete
* IDT and exception dispatch sanity

**Benchmarks** (`bench` builtin):

```
context_switch     :  <results pending>
kmalloc 4 KB       :  <results pending>
fs_write 1 KB      :  <results pending>
timer interrupt    :  <results pending>
```

Numbers depend on host CPU, since QEMU isn't a cycle-accurate target.

---

## Credits

Built with `x86_64-elf-gcc`, NASM, GNU make, and QEMU. Heavily inspired by classic UNIX, MINIX, and a steady diet of the OSDev wiki. Thanks to everyone who's ever written a tutorial on long mode, the PIC, or page tables. You know who you are.

---

## Project Structure

```
OS/
├── boot/             Stage1 MBR + Stage2 long-mode entry (NASM)
├── kernel/           kernel_main, init, panic, core glue
├── drivers/          VGA, serial, keyboard, timer, PIC
├── memory/           PMM, paging, heap
├── process/          PCB, task creation, context switch
├── scheduler/        Round-robin preemptive scheduler
├── syscall/          int 0x80 dispatch table
├── fs/               In-memory filesystem
├── shell/            Line editor, builtins, parser
├── editor/           Full-screen text editor
├── calc/             Expression parser and evaluator
├── security/         Users, login, password hashing
├── observability/    ps, top, meminfo, perf counters
├── benchmark/        Timed micro-benchmarks
├── test/             Kernel self-test suite
├── lib/              libc-lite: string, printf, memcpy
├── scripts/          Build helpers
├── docs/             Design notes
├── build/            Object files, ELF, disk image (generated)
├── link.ld           Kernel linker script
└── Makefile          Top-level build
```

---

## License

MIT. Do what you want, just keep the notice.
