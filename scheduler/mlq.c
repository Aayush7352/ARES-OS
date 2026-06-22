#include "scheduler.h"
#include "queue.h"
#include "process.h"
#include <stddef.h>

#define MLQ_LEVELS 4

/* Per-level quantum values: RT=2, HIGH=4, NORMAL=6, LOW=8 ticks */
static const uint32_t mlq_quanta[MLQ_LEVELS] = {2U, 4U, 6U, 8U};

static process_queue_t   mlq_queues[MLQ_LEVELS];
static scheduler_stats_t mlq_stats;
static uint32_t          mlq_level_ticks[MLQ_LEVELS];

/* Map process priority to MLQ level (0=highest, 3=lowest) */
static int mlq_level_of(uint32_t priority) {
    if (priority >= (uint32_t)PRIORITY_REALTIME) return 0;
    if (priority >= (uint32_t)PRIORITY_HIGH)     return 1;
    if (priority >= (uint32_t)PRIORITY_NORMAL)   return 2;
    return 3;
}

static void mlq_init(void) {
    for (int i = 0; i < MLQ_LEVELS; i++) {
        queue_init(&mlq_queues[i]);
        mlq_level_ticks[i] = 0;
    }
    mlq_stats.name                = "MLQ";
    mlq_stats.context_switches    = 0;
    mlq_stats.idle_ticks          = 0;
    mlq_stats.total_ticks         = 0;
    mlq_stats.processes_completed = 0;
    mlq_stats.queue_depth         = 0;
}

static void mlq_add(pcb_t *proc) {
    int level = mlq_level_of(proc->priority);
    queue_push(&mlq_queues[level], proc);
}

static void mlq_remove(pcb_t *proc) {
    for (int i = 0; i < MLQ_LEVELS; i++) {
        if (queue_contains(&mlq_queues[i], proc)) {
            queue_remove(&mlq_queues[i], proc);
            return;
        }
    }
}

static pcb_t *mlq_next(void) {
    for (int i = 0; i < MLQ_LEVELS; i++) {
        pcb_t *next = queue_pop(&mlq_queues[i]);
        if (next) {
            mlq_stats.context_switches++;
            /* Reset all level counters - new process gets fresh quantum */
            for (int j = 0; j < MLQ_LEVELS; j++)
                mlq_level_ticks[j] = 0;
            return next;
        }
    }
    return NULL;
}

static void mlq_tick(void) {
    mlq_stats.total_ticks++;
    if (current_process && current_process->pid > 1U) {
        int level = mlq_level_of(current_process->priority);
        mlq_level_ticks[level]++;
        if (mlq_level_ticks[level] >= mlq_quanta[level]) {
            scheduler_need_resched = 1;
        }
    } else {
        mlq_stats.idle_ticks++;
    }
}

static void mlq_get_stats(scheduler_stats_t *stats) {
    uint32_t depth = 0;
    for (int i = 0; i < MLQ_LEVELS; i++)
        depth += mlq_queues[i].count;
    mlq_stats.queue_depth = depth;
    *stats = mlq_stats;
    stats->name = "MLQ";
}

scheduler_ops_t mlq_ops = {
    .name      = "MLQ",
    .init      = mlq_init,
    .add       = mlq_add,
    .remove    = mlq_remove,
    .next      = mlq_next,
    .tick      = mlq_tick,
    .get_stats = mlq_get_stats
};
