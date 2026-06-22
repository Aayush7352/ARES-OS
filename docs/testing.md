# ARES OS, Testing and Benchmarks

A kernel without tests is a kernel that breaks on every refactor and nobody notices for a week. ARES ships a small test framework that runs inside the kernel, plus a benchmark harness that measures real timings against the timer counter. Both are wired into the build so `make test` and `make bench` Just Work.

## Test Framework

The framework is one header and one source file. It exposes a registration API, a runner, and a reporter. Tests live next to the code they cover and self-register at link time through a section attribute.

```c
typedef int (*test_fn_t)(void);

typedef struct {
    const char* group;
    const char* name;
    test_fn_t   fn;
} test_case_t;

#define REGISTER_TEST(grp, nm, fnref)                          \
    static const test_case_t _test_##fnref                    \
    __attribute__((used, section(".tests"))) = {              \
        .group = grp, .name = #nm, .fn = fnref                \
    }
```

The `.tests` section is bracketed by linker symbols. The runner walks from `__start_tests` to `__stop_tests`, calls every function, and counts the results.

```c
extern const test_case_t __start_tests[];
extern const test_case_t __stop_tests[];

void test_run_all(void) {
    int pass = 0, fail = 0;
    for (const test_case_t* t = __start_tests; t < __stop_tests; t++) {
        printf("[ run  ] %s::%s\n", t->group, t->name);
        int r = t->fn();
        if (r == 0) { printf("[  ok  ]\n"); pass++; }
        else        { printf("[ FAIL ] code=%d\n", r); fail++; }
    }
    printf("\n%d passed, %d failed\n", pass, fail);
}
```

A test returns 0 for success and non-zero for failure. There's a `TEST_ASSERT(cond)` macro that returns the line number on failure, so the report points at the bad line without a full stack trace.

```c
#define TEST_ASSERT(c) do { if (!(c)) return __LINE__; } while (0)
#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))
```

That's the entire framework. No mocks, no fixtures, no setup/teardown. If a test needs setup, it does it inline at the top.

## Scheduler Tests

The scheduler suite spins up synthetic processes that record when they run, then checks the recorded order against the policy's expected behavior.

```c
static int counters[8];

static void worker(void) {
    int id = (int)(uintptr_t)current->arg;
    for (int i = 0; i < 100; i++) { counters[id]++; sys_yield(); }
    sys_exit(0);
}

static int test_rr_fairness(void) {
    memset(counters, 0, sizeof(counters));
    for (int i = 0; i < 4; i++) spawn(worker, (void*)(uintptr_t)i);
    wait_all();
    for (int i = 0; i < 4; i++)
        TEST_ASSERT(counters[i] >= 95 && counters[i] <= 105);
    return 0;
}
REGISTER_TEST("sched", rr_fairness, test_rr_fairness);
```

Round Robin should give every worker roughly the same number of ticks. The tolerance is wide because the timer and the test setup aren't perfectly aligned. Tightening it would just make the test flaky.

Other scheduler tests cover: FCFS preserves submission order, Priority honors lower-numbered priorities, MLQ drains the real-time band before touching interactive.

## Memory Tests

The memory suite exercises the PMM, the VMM, and the heap, in that order.

```c
static int test_pmm_alloc_free(void) {
    void* p1 = pmm_alloc_page();
    void* p2 = pmm_alloc_page();
    TEST_ASSERT(p1 && p2 && p1 != p2);
    pmm_free_page(p1);
    void* p3 = pmm_alloc_page();
    TEST_ASSERT(p3 == p1);   /* first-free reuse */
    return 0;
}

static int test_heap_coalesce(void) {
    void* a = kmalloc(64);
    void* b = kmalloc(64);
    void* c = kmalloc(64);
    size_t used_before = heap_used();
    kfree(a); kfree(c); kfree(b);    /* middle free triggers merge */
    TEST_ASSERT(heap_used() < used_before);
    void* big = kmalloc(192);
    TEST_ASSERT(big != NULL);
    kfree(big);
    return 0;
}
```

The VMM test maps a fresh page, writes a sentinel, reads it back, unmaps it, and confirms that touching the address now panics in the controlled way. That last part needs the exception handler to call back into the test reporter instead of halting, so the test framework installs a one-shot fault handler before the unmap.

## File System Tests

The FS suite formats a scratch region, mounts it, and runs a sequence of operations.

```c
static int test_fs_roundtrip(void) {
    TEST_ASSERT_EQ(fs_format(),  ARES_OK);
    TEST_ASSERT_EQ(fs_mount(),   ARES_OK);

    int fd = fs_open("/hello.txt", O_CREATE | O_WRITE);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT_EQ(fs_write(fd, "hi", 2), 2);
    fs_close(fd);

    fd = fs_open("/hello.txt", O_READ);
    char buf[8] = {0};
    TEST_ASSERT_EQ(fs_read(fd, buf, sizeof(buf)), 2);
    TEST_ASSERT(buf[0] == 'h' && buf[1] == 'i');
    fs_close(fd);

    return 0;
}
```

Other FS tests cover: creating and listing a directory, hitting the inode limit and getting `ENOMEM`, writing past EOF and reading back zeros from a sparse region, the permission gate refusing a non-root write to a root-owned file.

## Benchmarks

Benchmarks share the timer-tick counter the scheduler uses. We don't have a high-resolution timer, but at 100 Hz the tick is granular enough to compare relative performance.

```c
extern volatile uint64_t timer_ticks;

#define BENCH_BEGIN(label) \
    uint64_t _t0 = timer_ticks; const char* _lbl = (label)
#define BENCH_END(iters) do {                                       \
    uint64_t _t = timer_ticks - _t0;                                \
    printf("%-28s %6lu ticks  %8lu ops/tick\n",                     \
           _lbl, (unsigned long)_t,                                 \
           (unsigned long)((iters) / (_t ? _t : 1)));               \
} while (0)
```

A typical scheduler benchmark spawns N short tasks and times the drain:

```c
void bench_sched_throughput(void) {
    BENCH_BEGIN("sched: 1000 yields");
    for (int i = 0; i < 1000; i++) sys_yield();
    BENCH_END(1000);
}
```

FS benchmarks measure block reads, full-file writes, and directory listings. Memory benchmarks measure `kmalloc/kfree` cycles at a few sizes (16, 256, 4096 bytes).

The output is a column-aligned table that's easy to diff between runs. A regression shows up as a row whose number got bigger. We keep last-known-good numbers in `bench/baseline.txt` and the build prints a warning if anything is more than 20% slower.

## Running

```text
make test        # build with TEST=1, kernel runs test_run_all then halts
make bench       # build with BENCH=1, kernel runs all benchmarks then halts
make qemu-test   # same as make test but inside QEMU with -nographic
```

A green test run looks like:

```text
[ run  ] sched::rr_fairness
[  ok  ]
[ run  ] sched::priority_order
[  ok  ]
[ run  ] mem::pmm_alloc_free
[  ok  ]
...
38 passed, 0 failed
```

A failed run halts after the report so you can read the screen. CI grabs the QEMU serial output and greps for "failed" being non-zero.

## What the Tests Buy

Every refactor in this kernel goes through `make test` before it lands. The suite isn't huge, but it covers the contracts that everything else relies on: the scheduler is fair, the PMM doesn't double-allocate, the heap coalesces, the FS round-trips data, the permission check rejects what it should. When one of those breaks, the diff that broke it is the smallest possible change, and that's the whole reason to write tests in a hobby kernel.
