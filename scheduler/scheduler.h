#ifndef ARES_SCHEDULER_H
#define ARES_SCHEDULER_H

#include <stdint.h>
#include "pcb.h"

#define QUANTUM_DEFAULT 3U   /* Default RR quantum in timer ticks */

typedef enum {
    SCHED_FCFS = 0,
    SCHED_RR,
    SCHED_PRIORITY,
    SCHED_MLQ,
    SCHED_COUNT
} scheduler_id_t;

typedef struct {
    const char *name;
    uint64_t    context_switches;
    uint64_t    idle_ticks;
    uint64_t    total_ticks;
    uint64_t    processes_completed;
    uint32_t    queue_depth;
} scheduler_stats_t;

typedef struct scheduler_ops {
    const char *name;
    void   (*init)(void);
    void   (*add)(pcb_t *proc);
    void   (*remove)(pcb_t *proc);
    pcb_t *(*next)(void);
    void   (*tick)(void);       /* Timer tick - NULL if cooperative only */
    void   (*get_stats)(scheduler_stats_t *stats);
} scheduler_ops_t;

/* Global dispatcher */
void           scheduler_init(scheduler_id_t id);
void           scheduler_add(pcb_t *proc);
void           scheduler_remove(pcb_t *proc);
pcb_t         *scheduler_next(void);
void           scheduler_tick(void);
void           scheduler_set(scheduler_id_t id);
scheduler_id_t scheduler_get_current(void);
void           scheduler_get_stats(scheduler_stats_t *stats);
const char    *scheduler_name(scheduler_id_t id);

/* Reschedule flag for ISR use */
extern volatile int scheduler_need_resched;

/* Per-algorithm ops tables */
extern scheduler_ops_t fcfs_ops;
extern scheduler_ops_t rr_ops;
extern scheduler_ops_t priority_ops;
extern scheduler_ops_t mlq_ops;

/* Benchmark API */
typedef struct {
    scheduler_id_t sched_id;
    int            num_processes;
    int            work_iterations;
    int            problem_size;
    uint64_t       start_tick;
    uint64_t       end_tick;
    uint64_t       elapsed_ticks;
    uint64_t       context_switches;
} benchmark_result_t;

void benchmark_run_scheduler(scheduler_id_t id, int num_procs, int work, int problem);
void benchmark_run_all(int num_procs, int work, int problem);
void benchmark_print_result(const benchmark_result_t *r);
void benchmark_print_header(void);

#endif /* ARES_SCHEDULER_H */
