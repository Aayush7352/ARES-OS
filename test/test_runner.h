#ifndef ARES_TEST_RUNNER_H
#define ARES_TEST_RUNNER_H

#include <stdint.h>
#include <stddef.h>

/*------------------------------------------------------------------------*/
/* ARES OS - Kernel Test Framework                                        */
/*                                                                        */
/* A tiny in-kernel test runner. Each test is a `void test_xxx(void)`     */
/* function that uses TEST_ASSERT(cond, msg) to record failures. When an  */
/* assertion fails, the function calls test_fail() and returns; the       */
/* runner then prints PASS or FAIL for that case and continues with the   */
/* next one. test_passed / test_failed accumulate across calls so the     */
/* shell can drive multiple suites and print a single grand summary.      */
/*------------------------------------------------------------------------*/

/* Records a failure and bails out of the current test function. */
#define TEST_ASSERT(cond, msg) do {                                       \
        if (!(cond)) {                                                    \
            test_fail((msg), __FILE__, __LINE__);                         \
            return;                                                       \
        }                                                                 \
    } while (0)

/* Declares (and defines, when used as a function header) a test case.
   Static to suppress -Wmissing-prototypes — tests are TU-local. */
#define TEST_RUN(name) static void test_##name(void)

typedef void (*test_fn_t)(void);

typedef struct {
    const char *name;
    test_fn_t   fn;
} test_case_t;

/* Run an array of test cases, printing PASS/FAIL for each. */
void test_run(const test_case_t *tests, int count);

/* Called from TEST_ASSERT - marks the in-flight test as failed. */
void test_fail(const char *msg, const char *file, int line);

/* Aggregate counters maintained by test_run() across all invocations. */
extern int test_passed;
extern int test_failed;

/* Per-suite entry points implemented in test_scheduler.c / _memory.c /  */
/* _fs.c.  Each calls test_run() with its own test_case_t[] table.       */
void test_scheduler_run_all(void);
void test_memory_run_all(void);
void test_fs_run_all(void);

/* Convenience: run every suite, then print a grand-total summary. */
void test_run_all(void);

#endif /* ARES_TEST_RUNNER_H */
