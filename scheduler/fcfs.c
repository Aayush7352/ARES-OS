#include "scheduler.h"
#include "queue.h"
#include <stddef.h>

static process_queue_t   fcfs_queue;
static scheduler_stats_t fcfs_stats;

static void fcfs_init(void) {
    queue_init(&fcfs_queue);
    fcfs_stats.name                = "FCFS";
    fcfs_stats.context_switches    = 0;
    fcfs_stats.idle_ticks          = 0;
    fcfs_stats.total_ticks         = 0;
    fcfs_stats.processes_completed = 0;
    fcfs_stats.queue_depth         = 0;
}

static void fcfs_add(pcb_t *proc) {
    queue_push(&fcfs_queue, proc);
}

static void fcfs_remove(pcb_t *proc) {
    queue_remove(&fcfs_queue, proc);
}

static pcb_t *fcfs_next(void) {
    pcb_t *next = queue_pop(&fcfs_queue);
    if (next) fcfs_stats.context_switches++;
    return next;
}

static void fcfs_get_stats(scheduler_stats_t *stats) {
    fcfs_stats.queue_depth = fcfs_queue.count;
    *stats = fcfs_stats;
    stats->name = "FCFS";
}

scheduler_ops_t fcfs_ops = {
    .name      = "FCFS",
    .init      = fcfs_init,
    .add       = fcfs_add,
    .remove    = fcfs_remove,
    .next      = fcfs_next,
    .tick      = NULL,
    .get_stats = fcfs_get_stats
};
