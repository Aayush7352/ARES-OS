//==============================================================================
// ARES OS - Main Kernel
//==============================================================================
// This is the heart of ARES OS. After the bootloader transitions the system
// to 64-bit long mode, the kernel entry trampoline (boot.asm) calls kernel_main.
//
// Responsibilities:
//   - Initialize all kernel subsystems
//   - Provide system initialization and coordination
//   - Enter the main kernel loop
//==============================================================================

#include "kernel.h"
#include "console.h"
#include "interrupt.h"
#include "irq.h"
#include "scheduler.h"
#include "process.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "ata.h"
#include "aresfs.h"
#include "syscall.h"
#include "keyboard.h"
#include "rtc.h"
#include "shell.h"
#include "auth.h"

//==============================================================================
// Early initialization (before BSS is cleared)
//==============================================================================
void kernel_early_init(void) {
    /* Called before BSS is cleared - minimal setup only */
}

//==============================================================================
// Demo tasks for scheduling verification
//==============================================================================

static void demo_task_fcfs(void);
static void demo_task_rr(void);
static void demo_task_priority(void);
static void demo_task_mlq(void);

/* Worker process — just yields repeatedly */
static void worker_yield(void) {
    int count = 0;
    while (1) {
        volatile int j;
        for (j = 0; j < 500000; j++) { __asm__ volatile("pause"); }
        count++;
        if (count >= 20) {
            console_printf("[worker] PID %d done (%s)\n",
                           current_process->pid,
                           scheduler_name(scheduler_get_current()));
            process_exit(0);
        }
        yield();
    }
}

/* Worker process — compute-bound */
static void worker_compute(void) {
    uint64_t sum = 0;
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 1000; j++) {
            sum += (uint64_t)(i * j);
        }
    }
    (void)sum;
    console_printf("[worker] PID %d (%s) compute done\n",
                   current_process->pid,
                   scheduler_name(scheduler_get_current()));
    process_exit(0);
}

static void demo_task_fcfs(void) {
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("");
    console_writeline("=== FCFS Scheduling Demo ===");
    scheduler_init(SCHED_FCFS);
    process_create("fcfs-a", worker_yield, PRIORITY_LOW);
    process_create("fcfs-b", worker_yield, PRIORITY_LOW);
    process_create("fcfs-c", worker_yield, PRIORITY_LOW);
}

static void demo_task_rr(void) {
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("");
    console_writeline("=== Round Robin Scheduling Demo ===");
    scheduler_init(SCHED_RR);
    process_create("rr-a", worker_yield, PRIORITY_NORMAL);
    process_create("rr-b", worker_yield, PRIORITY_NORMAL);
    process_create("rr-c", worker_yield, PRIORITY_NORMAL);
}

static void demo_task_priority(void) {
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("");
    console_writeline("=== Priority Scheduling Demo ===");
    scheduler_init(SCHED_PRIORITY);
    process_create("prio-lo", worker_compute, PRIORITY_LOW);
    process_create("prio-norm", worker_compute, PRIORITY_NORMAL);
    process_create("prio-hi", worker_compute, PRIORITY_HIGH);
}

static void demo_task_mlq(void) {
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("");
    console_writeline("=== MLQ Scheduling Demo ===");
    scheduler_init(SCHED_MLQ);
    process_create("mlq-lo", worker_compute, PRIORITY_LOW);
    process_create("mlq-norm", worker_compute, PRIORITY_NORMAL);
    process_create("mlq-hi", worker_compute, PRIORITY_HIGH);
    process_create("mlq-rt", worker_compute, PRIORITY_REALTIME);
}

//==============================================================================
// Main kernel entry point
//==============================================================================

void kernel_main(void) {
    /* Welcome banner */
    console_clear();
    console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);

    console_writeline("");
    console_writeline("  AAA   RRRR   EEEEE  SSSSS");
    console_writeline(" A   A  R   R  E      S    ");
    console_writeline(" AAAAA  RRRR   EEEE   SSSSS");
    console_writeline(" A   A  R  R   E          S");
    console_writeline(" A   A  R   R  EEEEE  SSSSS");
    console_writeline("");

    console_set_color(VGA_WHITE, VGA_BLACK);
    console_writeline("========================================");
    console_writeline("  ARES OS v0.1.0 - x86_64 Long Mode");
    console_writeline("  A Modern UNIX-Inspired Operating System");
    console_writeline("========================================");
    console_writeline("");

    /* Print boot information */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[kernel] Kernel loaded successfully at 0x100000");
    console_printf("[kernel] Architecture: %s\n", KERNEL_ARCH);
    console_printf("[kernel] Version: %s\n", KERNEL_VERSION);
    console_writeline("");

    /* Architecture already initialized by bootloader (long mode, GDT, paging) */

    /* Initialize interrupt handling */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[int] Initializing IDT...");
    interrupt_init();

    /* Initialize IRQ / PIC */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[irq] Initializing PIC...");
    irq_init();

    /* Initialize syscall interface */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[syscall] Initializing syscall interface...");
    syscall_init();

    /* Initialize process management */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[proc] Initializing process manager...");
    process_init();

    /* Initialize default scheduler (FCFS) */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[sched] Initializing default scheduler...");
    scheduler_init(SCHED_FCFS);

    /* Initialize physical memory manager (64 MiB RAM) */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[pmm] Initializing physical memory manager...");
    pmm_init(0x4000000);

    /* Initialize virtual memory manager */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[vmm] Initializing virtual memory manager...");
    vmm_init();

    /* Initialize kernel heap */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[heap] Initializing kernel heap...");
    heap_init();

    /* Initialize keyboard driver */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[kbd] Initializing keyboard driver...");
    keyboard_init();

    /* Initialize RTC */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[rtc] Initializing RTC...");
    rtc_init();

    /* Initialize filesystem (ATA driver + ARES FS) */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    fs_init();

    /* Initialize authentication system */
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("[auth] Initializing user accounts...");
    auth_init();

    /* System ready banner */
    console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    console_writeline("");
    console_writeline("========================================");
    console_writeline("  ARES OS is ready.");
    console_writeline("========================================");
    console_writeline("");

    /* Enable interrupts — IDT and PIC are now configured */
    __asm__ volatile("sti");

    /* Login prompt */
    auth_run_login();

    /* Task 1: Process creation and yield demonstration */
    console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    console_writeline("[kernel] Creating FCFS demo processes...");
    process_create("p1", demo_task_fcfs, PRIORITY_NORMAL);
    process_create("p2", demo_task_rr, PRIORITY_NORMAL);
    process_create("p3", demo_task_priority, PRIORITY_NORMAL);
    process_create("p4", demo_task_mlq, PRIORITY_NORMAL);

    /* Start with FCFS demo — set current to idle and yield */
    current_process = process_by_pid(1);
    console_set_color(VGA_WHITE, VGA_BLACK);
    console_writeline("[kernel] Starting scheduling demos...");

    yield();

    /* After all demos complete, we return here */
    console_set_color(VGA_GREEN, VGA_BLACK);
    console_writeline("");
    console_writeline("[kernel] All scheduling demos completed.");

    /* Show final scheduler stats */
    {
        scheduler_stats_t s;
        scheduler_get_stats(&s);
        console_printf("[sched] Final: switches=%u, completed=%u\n",
                       (unsigned int)s.context_switches,
                       (unsigned int)s.processes_completed);
    }

    /* Launch the shell */
    shell_main();

    /* Should never return */
    while (1) { __asm__ volatile("hlt"); }
}
