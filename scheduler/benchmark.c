#include "scheduler.h"
#include "process.h"
#include "console.h"
#include "irq.h"
#include <stddef.h>

/* Global benchmark control state.
 * Workers loop on this flag - benchmark driver clears it to terminate. */
static volatile int      benchmark_running    = 0;
static volatile uint64_t benchmark_work_iters = 10000U;

/* Worker entry point.
 * Each worker performs CPU-bound dummy work then yields, repeatedly.
 * Exits when benchmark_running is cleared by the driver. */
static void bench_worker(void) {
    while (benchmark_running) {
        volatile uint64_t i;
        for (i = 0; i < benchmark_work_iters; i++) {
            __asm__ volatile("pause");
        }
        yield();
    }
    process_exit(0);
}

/* Build a short process name "wNN" where NN is the worker index. */
static void bench_make_name(char *out, int idx) {
    out[0] = 'w';
    if (idx < 10) {
        out[1] = (char)('0' + idx);
        out[2] = '\0';
    } else {
        out[1] = (char)('0' + (idx / 10));
        out[2] = (char)('0' + (idx % 10));
        out[3] = '\0';
    }
}

void benchmark_print_header(void) {
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_printf("%-14s %-6s %-10s %-10s %-10s\n",
                   "Scheduler", "Procs", "Elapsed", "CtxSwitch", "Idle");
    console_printf("%-14s %-6s %-10s %-10s %-10s\n",
                   "---------", "-----", "-------", "---------", "----");
    console_set_color(VGA_WHITE, VGA_BLACK);
}

void benchmark_print_result(const benchmark_result_t *r) {
    if (!r) return;
    console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    console_printf("%-14s %-6d %-10lu %-10lu %-10s\n",
                   scheduler_name(r->sched_id),
                   r->num_processes,
                   (unsigned long)r->elapsed_ticks,
                   (unsigned long)r->context_switches,
                   "-");
    console_set_color(VGA_WHITE, VGA_BLACK);
}

void benchmark_run_scheduler(scheduler_id_t id, int num_procs, int work, int problem) {
    (void)problem;

    if (num_procs <= 0) num_procs = 1;
    if (num_procs > 16) num_procs = 16;
    if (work > 0) benchmark_work_iters = (uint64_t)work;

    console_writeline("");
    console_set_color(VGA_LIGHT_BROWN, VGA_BLACK);
    console_printf("=== Benchmark: %s (%d processes) ===\n",
                   scheduler_name(id), num_procs);
    console_set_color(VGA_WHITE, VGA_BLACK);

    /* Reset scheduler to target algorithm (this clears stats). */
    scheduler_init(id);

    /* Snapshot tick at start. */
    uint64_t start_tick = timer_ticks;
    benchmark_running   = 1;

    /* Spawn worker processes with varied priorities so priority/MLQ
     * schedulers exercise all queues. */
    for (int i = 0; i < num_procs; i++) {
        char name[8];
        bench_make_name(name, i);
        uint32_t prio = (uint32_t)i % (uint32_t)PRIORITY_COUNT;
        pcb_t *p = process_create(name, bench_worker, prio);
        (void)p;
    }

    /* Run for ~500 timer ticks while workers churn. */
    console_printf("[bench] Running for ~500 ticks...\n");
    uint64_t target_tick = start_tick + 500U;
    while (timer_ticks < target_tick) {
        __asm__ volatile("sti; hlt");
    }

    /* Signal workers to exit. They will call process_exit() on next loop. */
    benchmark_running = 0;

    /* Give workers a few ticks to drain. */
    uint64_t drain_target = timer_ticks + 20U;
    while (timer_ticks < drain_target) {
        __asm__ volatile("sti; hlt");
    }

    /* Collect stats. */
    scheduler_stats_t stats;
    scheduler_get_stats(&stats);

    benchmark_result_t result;
    result.sched_id         = id;
    result.num_processes    = num_procs;
    result.work_iterations  = work;
    result.problem_size     = problem;
    result.start_tick       = start_tick;
    result.end_tick         = timer_ticks;
    result.elapsed_ticks    = timer_ticks - start_tick;
    result.context_switches = stats.context_switches;

    console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    console_printf("[bench] %s: switches=%lu elapsed=%lu idle=%lu\n",
                   scheduler_name(id),
                   (unsigned long)stats.context_switches,
                   (unsigned long)result.elapsed_ticks,
                   (unsigned long)stats.idle_ticks);
    console_set_color(VGA_WHITE, VGA_BLACK);
}

void benchmark_run_all(int num_procs, int work, int problem) {
    console_writeline("");
    console_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
    console_writeline("============================================");
    console_writeline("  Scheduler Benchmark Suite - Running ALL");
    console_writeline("============================================");
    console_set_color(VGA_WHITE, VGA_BLACK);

    benchmark_print_header();

    benchmark_run_scheduler(SCHED_FCFS,     num_procs, work, problem);
    benchmark_run_scheduler(SCHED_RR,       num_procs, work, problem);
    benchmark_run_scheduler(SCHED_PRIORITY, num_procs, work, problem);
    benchmark_run_scheduler(SCHED_MLQ,      num_procs, work, problem);

    console_writeline("");
    console_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
    console_writeline("=== Benchmark Suite Complete ===");
    console_set_color(VGA_WHITE, VGA_BLACK);
}
