#include "scheduler.h"
#include "console.h"
#include <stddef.h>

static scheduler_ops_t *g_scheduler = NULL;
static scheduler_id_t   g_sched_id  = SCHED_FCFS;

volatile int scheduler_need_resched = 0;

void scheduler_set(scheduler_id_t id) {
    g_sched_id = id;
    switch (id) {
        case SCHED_FCFS:     g_scheduler = &fcfs_ops;     break;
        case SCHED_RR:       g_scheduler = &rr_ops;       break;
        case SCHED_PRIORITY: g_scheduler = &priority_ops; break;
        case SCHED_MLQ:      g_scheduler = &mlq_ops;      break;
        case SCHED_COUNT:    /* fall through */
        default:             return;
    }
    if (g_scheduler->init) g_scheduler->init();
}

void scheduler_init(scheduler_id_t id) {
    scheduler_set(id);
    console_printf("[sched] Scheduler: %s initialized\n", scheduler_name(id));
}

void scheduler_add(pcb_t *proc) {
    if (g_scheduler && g_scheduler->add)
        g_scheduler->add(proc);
}

void scheduler_remove(pcb_t *proc) {
    if (g_scheduler && g_scheduler->remove)
        g_scheduler->remove(proc);
}

pcb_t *scheduler_next(void) {
    if (g_scheduler && g_scheduler->next)
        return g_scheduler->next();
    return NULL;
}

void scheduler_tick(void) {
    if (g_scheduler && g_scheduler->tick)
        g_scheduler->tick();
}

scheduler_id_t scheduler_get_current(void) {
    return g_sched_id;
}

void scheduler_get_stats(scheduler_stats_t *stats) {
    if (g_scheduler && g_scheduler->get_stats)
        g_scheduler->get_stats(stats);
}

const char *scheduler_name(scheduler_id_t id) {
    switch (id) {
        case SCHED_FCFS:     return "FCFS";
        case SCHED_RR:       return "Round Robin";
        case SCHED_PRIORITY: return "Priority";
        case SCHED_MLQ:      return "MLQ";
        case SCHED_COUNT:    /* fall through */
        default:             return "Unknown";
    }
}
