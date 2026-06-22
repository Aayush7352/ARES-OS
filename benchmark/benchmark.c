/*==========================================================================*/
/* ARES OS - Benchmark Runner                                                */
/*==========================================================================*/

#include "benchmark.h"
#include "../kernel/console.h"
#include "../kernel/irq.h"
#include "../process/process.h"
#include "../process/pcb.h"
#include "../scheduler/scheduler.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"
#include "../fs/aresfs.h"

/*--------------------------------------------------------------------------*/
/* Timing helper — uses PIT timer_ticks as a coarse clock.                  */
/*--------------------------------------------------------------------------*/

static uint64_t ticks_start;

static void bench_start(void) {
    ticks_start = timer_ticks;
}

static uint64_t bench_end(void) {
    return timer_ticks - ticks_start;
}

/*--------------------------------------------------------------------------*/
/* Scheduler benchmark: create N workers, measure time to completion.       */
/*--------------------------------------------------------------------------*/

static void bench_worker_short(void) {
    volatile uint64_t x = 0;
    for (int i = 0; i < 10000; i++) x += (uint64_t)i;
    (void)x;
    process_exit(0);
}

static void bench_worker_long(void) {
    volatile uint64_t x = 0;
    for (int i = 0; i < 50000; i++) x += (uint64_t)i;
    (void)x;
    process_exit(0);
}

static void bench_scheduler_variant(const char *name, int sched_type) {
    scheduler_init(sched_type);
    process_create("b-worker", bench_worker_short, PRIORITY_NORMAL);
    process_create("b-worker", bench_worker_short, PRIORITY_NORMAL);
    process_create("b-worker", bench_worker_short, PRIORITY_NORMAL);
    process_create("b-worker", bench_worker_short, PRIORITY_NORMAL);
    process_create("b-worker", bench_worker_long, PRIORITY_REALTIME);

    bench_start();
    current_process = process_by_pid(1);
    yield();
    uint64_t elapsed = bench_end();

    console_printf("  %-10s: %u ticks\n", name, (unsigned int)elapsed);
}

void bench_scheduler(void) {
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("=== Scheduler Benchmarks ===");
    console_set_color(VGA_WHITE, VGA_BLACK);

    bench_scheduler_variant("FCFS", SCHED_FCFS);
    bench_scheduler_variant("RR", SCHED_RR);
    bench_scheduler_variant("Priority", SCHED_PRIORITY);
    bench_scheduler_variant("MLQ", SCHED_MLQ);
}

/*--------------------------------------------------------------------------*/
/* Filesystem benchmark: write/read throughput.                             */
/*--------------------------------------------------------------------------*/

void bench_filesystem(void) {
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("=== Filesystem Benchmarks ===");
    console_set_color(VGA_WHITE, VGA_BLACK);

    char test_data[256];
    for (int i = 0; i < 256; i++) test_data[i] = (char)(i & 0xFF);

    bench_start();
    int fd = aresfs_open("/bench_file", ARESFS_O_WRONLY | ARESFS_O_CREAT | ARESFS_O_TRUNC);
    if (fd < 0) { console_writeline("  FS write: SKIP (open failed)"); return; }

    uint64_t total = 0;
    for (int i = 0; i < 10; i++) {
        int n = aresfs_write(fd, test_data, 256);
        if (n > 0) total += (uint64_t)n;
    }
    aresfs_close(fd);
    uint64_t write_ticks = bench_end();

    bench_start();
    fd = aresfs_open("/bench_file", ARESFS_O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        uint64_t read_total = 0;
        for (int i = 0; i < 10; i++) {
            int n = aresfs_read(fd, buf, 256);
            if (n > 0) read_total += (uint64_t)n;
        }
        aresfs_close(fd);
        uint64_t read_ticks = bench_end();
        console_printf("  Write: %u bytes in %u ticks (%u B/tick)\n",
                       (unsigned int)total, (unsigned int)write_ticks,
                       write_ticks > 0 ? (unsigned int)(total / write_ticks) : 0U);
        console_printf("  Read:  %u bytes in %u ticks (%u B/tick)\n",
                       (unsigned int)read_total, (unsigned int)read_ticks,
                       read_ticks > 0 ? (unsigned int)(read_total / read_ticks) : 0U);
    }
}

/*--------------------------------------------------------------------------*/
/* Memory benchmark: malloc/free throughput.                                */
/*--------------------------------------------------------------------------*/

void bench_memory(void) {
    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("=== Memory Benchmarks ===");
    console_set_color(VGA_WHITE, VGA_BLACK);

    bench_start();
    void *ptrs[32];
    for (int i = 0; i < 20; i++) {
        ptrs[i] = kmalloc(64);
    }
    for (int i = 0; i < 20; i++) {
        if (ptrs[i]) kfree(ptrs[i]);
    }
    uint64_t alloc_ticks = bench_end();
    console_printf("  20x alloc(64)/free: %u ticks\n", (unsigned int)alloc_ticks);

    bench_start();
    void *big = kmalloc(4096);
    uint64_t big_ticks = bench_end();
    if (big) kfree(big);
    console_printf("  alloc(4096): %u ticks\n", (unsigned int)big_ticks);

    /* PMM benchmark */
    bench_start();
    uint64_t before_free = pmm_get_free_count();
    void *pg = pmm_alloc_page();
    uint64_t after_free = pmm_get_free_count();
    uint64_t pmm_ticks = bench_end();
    if (pg) pmm_free_page(pg);
    console_printf("  pmm_alloc/free: %u ticks (%u pages delta)\n",
                   (unsigned int)pmm_ticks,
                   (unsigned int)(before_free - after_free));
}

/*--------------------------------------------------------------------------*/
/* Run all benchmarks.                                                      */
/*--------------------------------------------------------------------------*/

void bench_run_all(void) {
    console_writeline("");
    console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    console_writeline("========================================");
    console_writeline("  ARES OS Benchmark Suite");
    console_writeline("========================================");
    console_set_color(VGA_WHITE, VGA_BLACK);
    console_writeline("");

    bench_scheduler();
    console_writeline("");
    bench_filesystem();
    console_writeline("");
    bench_memory();

    console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    console_writeline("========================================");
    console_writeline("  Benchmarks complete.");
    console_writeline("========================================");
    console_set_color(VGA_WHITE, VGA_BLACK);
}
