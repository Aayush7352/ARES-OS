#include "scheduler.h"
#include "queue.h"
#include "process.h"
#include <stddef.h>

static process_queue_t   rr_queue;
static scheduler_stats_t rr_stats;
static uint32_t          rr_current_ticks = 0;

static void rr_init(void) {
    queue_init(&rr_queue);
    rr_stats.name                = "Round Robin";
    rr_stats.context_switches    = 0;
    rr_stats.idle_ticks          = 0;
    rr_stats.total_ticks         = 0;
    rr_stats.processes_completed = 0;
    rr_stats.queue_depth         = 0;
    rr_current_ticks             = 0;
}

static void rr_add(pcb_t *proc) {
    queue_push(&rr_queue, proc);
}

static void rr_remove(pcb_t *proc) {
    queue_remove(&rr_queue, proc);
}

static pcb_t *rr_next(void) {
    pcb_t *next = queue_pop(&rr_queue);
    if (next) {
        rr_stats.context_switches++;
        rr_current_ticks = 0;
    }
    return next;
}

static void rr_tick(void) {
    rr_stats.total_ticks++;
    /* PID 1 is the idle process; do not count its quantum */
    if (current_process && current_process->pid > 1U) {
        rr_current_ticks++;
        if (rr_current_ticks >= QUANTUM_DEFAULT) {
            scheduler_need_resched = 1;
        }
    } else {
        rr_stats.idle_ticks++;
    }
}

static void rr_get_stats(scheduler_stats_t *stats) {
    rr_stats.queue_depth = rr_queue.count;
    *stats = rr_stats;
    stats->name = "Round Robin";
}

scheduler_ops_t rr_ops = {
    .name      = "Round Robin",
    .init      = rr_init,
    .add       = rr_add,
    .remove    = rr_remove,
    .next      = rr_next,
    .tick      = rr_tick,
    .get_stats = rr_get_stats
};
