/*==========================================================================*/
/* ARES OS - Observability: kernel log, metrics, top display                 */
/*==========================================================================*/

#include "observability.h"
#include "../kernel/console.h"
#include "../kernel/irq.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"
#include "../scheduler/scheduler.h"
#include "../drivers/keyboard.h"
#include "../process/process.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"

/*--------------------------------------------------------------------------*/
/* Simple ring-buffer kernel log.                                           */
/*--------------------------------------------------------------------------*/

static char log_buf[LOG_BUF_SIZE];
static size_t log_pos;

void log_write(const char *msg) {
    if (!msg) return;
    for (size_t i = 0; msg[i] != '\0' && log_pos < LOG_BUF_SIZE - 1; i++) {
        log_buf[log_pos++] = msg[i];
    }
    log_buf[log_pos] = '\0';
}

const char *log_read(size_t *len_out) {
    if (len_out) *len_out = log_pos;
    return log_buf;
}

/*--------------------------------------------------------------------------*/
/* System metrics collection.                                               */
/*--------------------------------------------------------------------------*/

void metrics_collect(system_metrics_t *m) {
    if (!m) return;
    m->uptime_ticks = timer_ticks;

    scheduler_stats_t s;
    scheduler_get_stats(&s);
    m->ctx_switches = s.context_switches;
    m->interrupts_served = s.processes_completed;

    heap_stats(&m->heap_used, &m->heap_free, &m->heap_free);
    m->mem_free_pages = pmm_get_free_count();
    m->mem_used_pages = pmm_get_used_count();
    m->process_count = process_count;

    pcb_t *buf[32];
    int count = pcb_enum_active(buf, 32);
    m->active_fds = 0;
    (void)count;
}

/*--------------------------------------------------------------------------*/
/* Top display — uses direct VGA writes for full-screen.                    */
/*--------------------------------------------------------------------------*/

void top_display(void) {
    console_clear();

    system_metrics_t m;
    metrics_collect(&m);

    console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    console_writeline("ARES OS System Monitor (top)");
    console_set_color(VGA_WHITE, VGA_BLACK);
    console_writeline("==============================");

    console_printf("Uptime        : %lu ticks\n", (unsigned long)m.uptime_ticks);
    console_printf("Context-switches: %lu\n", (unsigned long)m.ctx_switches);
    console_printf("Memory used   : %lu pages (%lu KiB)\n",
                   (unsigned long)m.mem_used_pages,
                   (unsigned long)(m.mem_used_pages * 4));
    console_printf("Memory free   : %lu pages (%lu KiB)\n",
                   (unsigned long)m.mem_free_pages,
                   (unsigned long)(m.mem_free_pages * 4));
    console_printf("Heap used     : %lu bytes\n", (unsigned long)m.heap_used);
    console_printf("Processes     : %u\n", (unsigned)m.process_count);
    console_writeline("");

    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("Active processes:");
    console_set_color(VGA_WHITE, VGA_BLACK);
    console_printf("%-4s %-16s %-8s %-8s\n", "PID", "NAME", "STATE", "TICKS");

    pcb_t *buf[MAX_PROCESSES];
    int cnt = pcb_enum_active(buf, MAX_PROCESSES);
    for (int i = 0; i < cnt; i++) {
        console_printf("%-4u %-16s %-8s %-8u\n",
                       buf[i]->pid, buf[i]->name,
                       process_state_str(buf[i]->state),
                       buf[i]->total_ticks);
    }

    console_writeline("");
    console_printf("Press any key to return...");
    while (!keyboard_has_data()) { __asm__ volatile("hlt"); }
    keyboard_getchar();
    console_clear();
}
