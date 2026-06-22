/*==========================================================================*/
/* ARES OS - Scheduler tests                                                  */
/*                                                                            */
/* The runner itself executes inside the shell process, so anything we do    */
/* here happens against a live scheduler with at least the idle and shell    */
/* processes queued.  Tests therefore avoid scheduler_init() (which would    */
/* wipe the live ready queue) and clean up every process they spawn before  */
/* returning.                                                                 */
/*==========================================================================*/

#include <stdint.h>
#include <stddef.h>

#include "test_runner.h"
#include "console.h"
#include "process.h"
#include "pcb.h"
#include "scheduler.h"

/*--------------------------------------------------------------------------*/
/* Local string helper - kernel has no libc.                                */
/*--------------------------------------------------------------------------*/

static int ts_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/*--------------------------------------------------------------------------*/
/* Shared worker state used by the cooperative yield test.                  */
/*--------------------------------------------------------------------------*/

static volatile int g_yield_runs = 0;

static void test_yield_worker(void) {
    /* Run twice so the test can observe a context switch in both             */
    /* directions, then exit cleanly through the scheduler.                  */
    g_yield_runs++;
    yield();
    g_yield_runs++;
    process_exit(0);
}

/*--------------------------------------------------------------------------*/
/* Worker for the create test - spins forever; the test will kill it.       */
/*--------------------------------------------------------------------------*/

static void test_spin_worker(void) {
    while (1) yield();
}

/*--------------------------------------------------------------------------*/
/* Tests                                                                    */
/*--------------------------------------------------------------------------*/

TEST_RUN(sched_create) {
    /* Snapshot the active set so we can verify three new entries appear. */
    pcb_t *before[MAX_PROCESSES];
    int    n_before = pcb_enum_active(before, MAX_PROCESSES);

    pcb_t *p1 = process_create("t_c1", test_spin_worker, PRIORITY_NORMAL);
    pcb_t *p2 = process_create("t_c2", test_spin_worker, PRIORITY_NORMAL);
    pcb_t *p3 = process_create("t_c3", test_spin_worker, PRIORITY_NORMAL);

    TEST_ASSERT(p1 != NULL, "process_create returned NULL for p1");
    TEST_ASSERT(p2 != NULL, "process_create returned NULL for p2");
    TEST_ASSERT(p3 != NULL, "process_create returned NULL for p3");

    pcb_t *after[MAX_PROCESSES];
    int    n_after = pcb_enum_active(after, MAX_PROCESSES);
    TEST_ASSERT(n_after >= n_before + 3, "active count did not grow by 3");

    /* Locate each new PCB by pid in the active enumeration. */
    int found = 0;
    for (int i = 0; i < n_after; i++) {
        if (after[i] == p1 || after[i] == p2 || after[i] == p3) found++;
    }
    TEST_ASSERT(found == 3, "newly-created processes not in active table");

    /* Tear down before returning so other tests start from a clean state. */
    (void)process_kill(p1->pid);
    (void)process_kill(p2->pid);
    (void)process_kill(p3->pid);
}

TEST_RUN(sched_yield) {
    /* Capture context-switch baseline before spawning the worker. */
    scheduler_stats_t s_before;
    scheduler_get_stats(&s_before);

    g_yield_runs = 0;
    pcb_t *p = process_create("t_yield", test_yield_worker, PRIORITY_NORMAL);
    TEST_ASSERT(p != NULL, "process_create failed");

    /* Yield repeatedly so the worker is scheduled, runs both halves, then  */
    /* terminates itself via process_exit(0).  20 round-trips is plenty     */
    /* even on a busy idle + shell + worker mix.                            */
    for (int i = 0; i < 20; i++) yield();

    TEST_ASSERT(g_yield_runs >= 2, "yield worker did not run twice");

    scheduler_stats_t s_after;
    scheduler_get_stats(&s_after);
    TEST_ASSERT(s_after.context_switches > s_before.context_switches,
                "context_switches counter did not advance");
}

TEST_RUN(sched_fcfs) {
    /* Read-only probe of the scheduler dispatcher: we must not call         */
    /* scheduler_init() here because it would discard the live ready queue. */
    scheduler_id_t cur = scheduler_get_current();
    TEST_ASSERT(cur >= SCHED_FCFS && cur < SCHED_COUNT,
                "scheduler_get_current out of range");

    const char *fcfs = scheduler_name(SCHED_FCFS);
    TEST_ASSERT(fcfs != NULL, "scheduler_name(FCFS) returned NULL");
    TEST_ASSERT(ts_strcmp(fcfs, "FCFS") == 0,
                "scheduler_name(FCFS) mismatch");

    scheduler_stats_t s;
    scheduler_get_stats(&s);
    TEST_ASSERT(s.name != NULL, "scheduler stats name is NULL");
}

/*--------------------------------------------------------------------------*/
/* Suite entry point.                                                       */
/*--------------------------------------------------------------------------*/

static const test_case_t sched_tests[] = {
    { "sched_create", test_sched_create },
    { "sched_yield",  test_sched_yield  },
    { "sched_fcfs",   test_sched_fcfs   },
};

void test_scheduler_run_all(void) {
    int n = (int)(sizeof(sched_tests) / sizeof(sched_tests[0]));
    test_run(sched_tests, n);
}
