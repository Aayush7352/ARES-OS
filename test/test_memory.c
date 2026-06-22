/*==========================================================================*/
/* ARES OS - Memory subsystem tests                                           */
/*                                                                            */
/* Exercise the physical page allocator and the kernel heap.  Every test     */
/* releases what it allocated so the suite is idempotent and does not leak   */
/* pages across the larger test runner invocation.                          */
/*==========================================================================*/

#include <stdint.h>
#include <stddef.h>

#include "test_runner.h"
#include "console.h"
#include "pmm.h"
#include "heap.h"

/*--------------------------------------------------------------------------*/
/* PMM: allocate one page, free it, check the free-count returns to its     */
/* baseline.                                                                */
/*--------------------------------------------------------------------------*/

TEST_RUN(pmm_alloc_free) {
    uint64_t free_before = pmm_get_free_count();
    TEST_ASSERT(free_before > 0U, "no free pages reported by PMM");

    void *page = pmm_alloc_page();
    TEST_ASSERT(page != NULL, "pmm_alloc_page returned NULL");

    uint64_t free_after_alloc = pmm_get_free_count();
    TEST_ASSERT(free_after_alloc + 1U == free_before,
                "free count did not drop by 1 after alloc");

    pmm_free_page(page);

    uint64_t free_after_free = pmm_get_free_count();
    TEST_ASSERT(free_after_free == free_before,
                "free count did not restore after free");
}

/*--------------------------------------------------------------------------*/
/* Heap: kmalloc a block, write to it, free it.  Confirm we get a non-NULL  */
/* pointer and that the byte we wrote is the byte we read back.            */
/*--------------------------------------------------------------------------*/

TEST_RUN(heap_alloc_free) {
    uint64_t used_before = 0U;
    uint64_t free_before = 0U;
    uint64_t largest_before = 0U;
    heap_stats(&used_before, &free_before, &largest_before);

    /* Use a small size so it always fits even on a busy heap. */
    uint8_t *p = (uint8_t *)kmalloc(64U);
    TEST_ASSERT(p != NULL, "kmalloc(64) returned NULL");

    /* Touch every byte to catch obvious overlap with another allocation. */
    for (size_t i = 0; i < 64U; i++) {
        p[i] = (uint8_t)(i & 0xFFU);
    }
    for (size_t i = 0; i < 64U; i++) {
        TEST_ASSERT(p[i] == (uint8_t)(i & 0xFFU),
                    "heap block contents were corrupted");
    }

    kfree(p);

    uint64_t used_after = 0U;
    uint64_t free_after = 0U;
    uint64_t largest_after = 0U;
    heap_stats(&used_after, &free_after, &largest_after);

    /* After kfree the bookkeeping should be back to where it started. */
    TEST_ASSERT(used_after == used_before,
                "heap used bytes did not restore after kfree");
}

/*--------------------------------------------------------------------------*/
/* Heap fragmentation: allocate several blocks, free every other one, and  */
/* check the freed bytes are returned to the pool.                         */
/*--------------------------------------------------------------------------*/

#define TEST_HEAP_BLOCKS 6

TEST_RUN(heap_fragmentation) {
    void *blocks[TEST_HEAP_BLOCKS];

    uint64_t used_before = 0U;
    uint64_t free_before = 0U;
    uint64_t largest_before = 0U;
    heap_stats(&used_before, &free_before, &largest_before);

    /* Mix of sizes drives the first-fit list into a non-trivial state. */
    static const size_t sizes[TEST_HEAP_BLOCKS] = {
        32U, 128U, 64U, 256U, 48U, 96U
    };

    for (int i = 0; i < TEST_HEAP_BLOCKS; i++) {
        blocks[i] = kmalloc(sizes[i]);
        TEST_ASSERT(blocks[i] != NULL, "kmalloc failed mid-loop");
    }

    /* Free even-indexed blocks first, then odd-indexed - exercises the    */
    /* free-list coalescing path. */
    for (int i = 0; i < TEST_HEAP_BLOCKS; i += 2) {
        kfree(blocks[i]);
        blocks[i] = NULL;
    }
    for (int i = 1; i < TEST_HEAP_BLOCKS; i += 2) {
        kfree(blocks[i]);
        blocks[i] = NULL;
    }

    uint64_t used_after = 0U;
    uint64_t free_after = 0U;
    uint64_t largest_after = 0U;
    heap_stats(&used_after, &free_after, &largest_after);

    TEST_ASSERT(used_after == used_before,
                "heap used bytes did not return to baseline");
}

/*--------------------------------------------------------------------------*/
/* Suite entry point.                                                       */
/*--------------------------------------------------------------------------*/

static const test_case_t memory_tests[] = {
    { "pmm_alloc_free",    test_pmm_alloc_free    },
    { "heap_alloc_free",   test_heap_alloc_free   },
    { "heap_fragmentation", test_heap_fragmentation },
};

void test_memory_run_all(void) {
    int n = (int)(sizeof(memory_tests) / sizeof(memory_tests[0]));
    test_run(memory_tests, n);
}
