#include "scheduler.h"
#include "queue.h"
#include "process.h"
#include <stddef.h>

#define PRIO_QUEUES PRIORITY_COUNT

static process_queue_t   priority_queues[PRIO_QUEUES];
static scheduler_stats_t prio_stats;
static uint32_t          prio_current_ticks = 0;

static void prio_init(void) {
    for (int i = 0; i < PRIO_QUEUES; i++)
        queue_init(&priority_queues[i]);
    prio_stats.name                = "Priority";
    prio_stats.context_switches    = 0;
    prio_stats.idle_ticks          = 0;
    prio_stats.total_ticks         = 0;
    prio_stats.processes_completed = 0;
    prio_stats.queue_depth         = 0;
    prio_current_ticks             = 0;
}

static void prio_add(pcb_t *proc) {
    uint32_t level = proc->priority;
    if (level >= (uint32_t)PRIO_QUEUES) level = (uint32_t)PRIO_QUEUES - 1U;
    queue_push(&priority_queues[level], proc);
}

static void prio_remove(pcb_t *proc) {
    for (int i = 0; i < PRIO_QUEUES; i++) {
        if (queue_contains(&priority_queues[i], proc)) {
            queue_remove(&priority_queues[i], proc);
            return;
        }
    }
}

static pcb_t *prio_next(void) {
    /* Scan from highest priority (REALTIME=3) down to LOW=0 */
    for (int i = PRIO_QUEUES - 1; i >= 0; i--) {
        pcb_t *next = queue_pop(&priority_queues[i]);
        if (next) {
            prio_stats.context_switches++;
            prio_current_ticks = 0;
            return next;
        }
    }
    return NULL;
}

static void prio_tick(void) {
    prio_stats.total_ticks++;
    if (current_process && current_process->pid > 1U) {
        prio_current_ticks++;
        if (prio_current_ticks >= QUANTUM_DEFAULT) {
            scheduler_need_resched = 1;
        }
    } else {
        prio_stats.idle_ticks++;
    }
}

static void prio_get_stats(scheduler_stats_t *stats) {
    uint32_t depth = 0;
    for (int i = 0; i < PRIO_QUEUES; i++)
        depth += priority_queues[i].count;
    prio_stats.queue_depth = depth;
    *stats = prio_stats;
    stats->name = "Priority";
}

scheduler_ops_t priority_ops = {
    .name      = "Priority",
    .init      = prio_init,
    .add       = prio_add,
    .remove    = prio_remove,
    .next      = prio_next,
    .tick      = prio_tick,
    .get_stats = prio_get_stats
};
