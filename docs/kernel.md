# ARES OS, Kernel Core

The kernel is the single binary that runs after stage2 hands off control. It owns the IDT, the PIC, the page tables, the PCB pool, and the idle loop. This document walks through what happens between the long jump into 64-bit code and the first scheduler tick.

## Entry Point

The linker pins `_start` at `0x100000`. Stage2 jumps there directly. The first thing `_start` does is set up a known stack and clear BSS, because C code can't run safely without either.

```c
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

void _start(boot_info_t* info) {
    /* Stack is already at 0x10A000 from stage2, but we reset it
       to a symbol we control so the linker can move it later. */
    asm volatile ("mov %0, %%rsp" :: "r"(&__stack_top));

    /* Zero BSS. Globals must read as zero. */
    for (uint8_t* p = __bss_start; p < __bss_end; p++) *p = 0;

    kernel_main(info);
    for (;;) asm volatile ("hlt");
}
```

The BSS clear is a hot loop, but it runs once and the kernel BSS is under 64 KB, so a byte-at-a-time write is fine. The infinite `hlt` after `kernel_main` is defensive; if anything ever returns from `kernel_main`, we don't want to triple-fault into a reboot.

## kernel_main

`kernel_main` is the bring-up sequence. It runs with interrupts disabled until the very end. The order isn't decorative, each step has a real dependency on the one before it.

```c
void kernel_main(boot_info_t* info) {
    console_init();
    printf("ARES OS booting...\n");

    interrupt_init();   /* IDT, exception stubs */
    irq_init();         /* PIC remap, mask everything */

    pmm_init(info);     /* page bitmap from memory map */
    vmm_init();         /* kernel page tables */
    heap_init();        /* first-fit allocator */

    ata_init();
    fs_mount();

    process_init();     /* PCB pool, idle task */
    scheduler_init(SCHED_RR);

    keyboard_init();
    rtc_init();

    irq_unmask(IRQ_TIMER);
    irq_unmask(IRQ_KEYBOARD);

    asm volatile ("sti");
    shell_start();       /* enqueues shell as PID 1 */

    idle_loop();         /* never returns */
}
```

If any of the init calls returns a non-`ARES_OK` status, the kernel logs the failure to the kernel ring buffer, prints a panic banner, and halts. We don't try to recover, because the only recovery strategy that actually works is a reboot.

## Console

The console is the simplest subsystem and the one we need first. It owns the VGA framebuffer at `0xB8000`, an 80x25 grid where each cell is two bytes: an ASCII character and an attribute byte (foreground/background nibble).

```c
typedef struct {
    uint16_t* fb;       /* 0xB8000 */
    uint8_t   row;
    uint8_t   col;
    uint8_t   attr;     /* default: gray on black, 0x07 */
} console_t;

void console_putc(char c);
void console_puts(const char* s);
int  printf(const char* fmt, ...);
```

`printf` supports `%d`, `%u`, `%x`, `%s`, `%c`, and `%p`. No floats, no width specifiers beyond a basic `%08x`. When the cursor would go past row 24, we memmove rows 1..24 up by one and clear the last row. That's the entire scrolling implementation.

## Interrupts

`interrupt_init` builds a 256-entry IDT. Slots 0..31 get exception handlers; 32..47 get IRQ stubs after PIC remap; the rest start as a default "unhandled" handler that panics with the vector number.

Exception handlers dump registers and halt. The dump is the same format every time, so a crash on instruction X always produces a comparable trace.

```c
typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

void exception_dump(interrupt_frame_t* f);
```

The PIC remap is the standard ICW1/ICW2/ICW3/ICW4 dance: master to `0x20-0x27`, slave to `0x28-0x2F`. Without it, the timer would fire as exception 8 (double fault), which is exactly the kind of mystery you don't want at boot.

## Process Init

`process_init` allocates the PCB pool, builds the free list, and creates PID 0, the idle task. The idle task's body is `for (;;) hlt;`. It exists so the scheduler always has something to pick when every other process is blocked.

```c
typedef struct pcb {
    uint64_t pid;
    uint64_t rsp;          /* saved kernel stack pointer */
    uint64_t rip;          /* resume point */
    uint32_t state;        /* READY, RUNNING, BLOCKED, ZOMBIE */
    uint32_t priority;
    uint64_t quantum_left;
    char     name[16];
    int      fd_table[8];
    struct pcb* next;
} pcb_t;
```

The pool is a fixed-size array of 64 PCBs. Allocation is O(1) via a bitmap. PIDs are slot indices, which means PID reuse is possible but the kernel logs every exit, so the log is the source of truth.

## Idle Loop

The idle loop is where the BSP ends up when no work is ready. It's also the safety net for the scheduler: if `next()` ever returns NULL, we fall back into idle rather than crashing.

```c
void idle_loop(void) {
    for (;;) {
        asm volatile ("sti; hlt");
    }
}
```

`sti; hlt` is one instruction pair on x86. The CPU re-enables interrupts and halts atomically, so we never miss an interrupt by enabling and halting separately. This is a small detail that took us a long time to learn.

## Status Codes

Every kernel API that can fail returns `ares_status_t`. We never use errno-style globals, because there's no thread-local storage and no libc.

```c
typedef enum {
    ARES_OK      = 0,
    ARES_ENOMEM  = -1,
    ARES_EIO     = -2,
    ARES_ENOENT  = -3,
    ARES_EPERM   = -4,
    ARES_EINVAL  = -5,
    ARES_EBUSY   = -6,
} ares_status_t;
```

The shell and tests both rely on these codes being stable. Renaming one is a breaking change across the tree.
