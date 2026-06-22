/*==========================================================================*/
/* ARES OS - Kernel Test Runner                                              */
/*                                                                            */
/* Implements the tiny driver behind TEST_ASSERT().  test_run() walks an     */
/* array of test cases, clears the per-case failure flag, invokes the        */
/* function, then prints PASS or FAIL.  Counters are exposed for callers     */
/* that want to print a grand summary after several suites have run.        */
/*==========================================================================*/

#include "test_runner.h"
#include "console.h"

/* Aggregate counters - visible to suite-level code that wants a summary. */
int test_passed = 0;
int test_failed = 0;

/* Per-case state.  Set to 1 inside test_fail() so test_run() knows the    */
/* current test reported a failure even after it returned via TEST_ASSERT. */
static int current_failed;

void test_fail(const char *msg, const char *file, int line) {
    current_failed = 1;
    console_set_color(VGA_LIGHT_RED, VGA_BLACK);
    console_printf("    assert: %s (%s:%d)\n",
                   msg ? msg : "(no message)",
                   file ? file : "?",
                   line);
    console_set_color(VGA_WHITE, VGA_BLACK);
}

void test_run(const test_case_t *tests, int count) {
    if (tests == NULL || count <= 0) return;

    for (int i = 0; i < count; i++) {
        const test_case_t *tc = &tests[i];
        if (tc->fn == NULL) continue;

        current_failed = 0;

        console_printf("  [%s] ", tc->name ? tc->name : "?");
        tc->fn();

        if (current_failed) {
            console_set_color(VGA_LIGHT_RED, VGA_BLACK);
            console_writeline("FAIL");
            console_set_color(VGA_WHITE, VGA_BLACK);
            test_failed++;
        } else {
            console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
            console_writeline("PASS");
            console_set_color(VGA_WHITE, VGA_BLACK);
            test_passed++;
        }
    }
}

void test_run_all(void) {
    /* Reset the grand totals so consecutive runs are comparable. */
    test_passed = 0;
    test_failed = 0;

    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("============================================");
    console_writeline("  ARES OS Kernel Test Suite");
    console_writeline("============================================");
    console_set_color(VGA_WHITE, VGA_BLACK);

    console_set_color(VGA_LIGHT_BROWN, VGA_BLACK);
    console_writeline("[scheduler]");
    console_set_color(VGA_WHITE, VGA_BLACK);
    test_scheduler_run_all();

    console_set_color(VGA_LIGHT_BROWN, VGA_BLACK);
    console_writeline("[memory]");
    console_set_color(VGA_WHITE, VGA_BLACK);
    test_memory_run_all();

    console_set_color(VGA_LIGHT_BROWN, VGA_BLACK);
    console_writeline("[filesystem]");
    console_set_color(VGA_WHITE, VGA_BLACK);
    test_fs_run_all();

    console_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console_writeline("============================================");
    console_printf("  Summary: %d passed, %d failed\n",
                   test_passed, test_failed);
    console_writeline("============================================");
    console_set_color(VGA_WHITE, VGA_BLACK);
}
