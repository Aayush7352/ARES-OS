#include "pmm.h"

/*------------------------------------------------------------------------*/
/* Layout                                                                 */
/*                                                                        */
/*   total pages   = MEM_PHYSICAL_END / PAGE_SIZE = 64 MiB / 4 KiB = 16384 */
/*   bitmap words  = 16384 / 64                  = 256 uint64_t           */
/*                                                                        */
/* Bit semantics: 1 = free, 0 = used.                                    */
/*------------------------------------------------------------------------*/

#define PMM_TOTAL_PAGES   ((uint64_t)MEM_PHYSICAL_END / (uint64_t)PAGE_SIZE)
#define PMM_BITMAP_WORDS  (PMM_TOTAL_PAGES / 64ULL)

/* Linker-provided kernel image bounds (see link.ld). */
extern char _kernel_start[];
extern char _kernel_end[];

/*------------------------------------------------------------------------*/
/* State                                                                  */
/*------------------------------------------------------------------------*/
static uint64_t pmm_bitmap[PMM_BITMAP_WORDS];
static uint64_t pmm_total_pages;
static uint64_t pmm_free_pages;
static uint64_t pmm_reserved_pages;

/*------------------------------------------------------------------------*/
/* Bitmap primitives                                                      */
/*------------------------------------------------------------------------*/
static inline void bitmap_set_free(uint64_t page_idx) {
    pmm_bitmap[page_idx >> 6U] |= (1ULL << (page_idx & 63ULL));
}

static inline void bitmap_set_used(uint64_t page_idx) {
    pmm_bitmap[page_idx >> 6U] &= ~(1ULL << (page_idx & 63ULL));
}

static inline bool bitmap_is_free(uint64_t page_idx) {
    return ((pmm_bitmap[page_idx >> 6U] >> (page_idx & 63ULL)) & 1ULL) != 0ULL;
}

/*------------------------------------------------------------------------*/
/* Mark a [start, end) byte range as reserved (not free).                 */
/*------------------------------------------------------------------------*/
static void mark_range_used(uint64_t start, uint64_t end) {
    uint64_t s = start / (uint64_t)PAGE_SIZE;
    uint64_t e = (end + (uint64_t)PAGE_SIZE - 1ULL) / (uint64_t)PAGE_SIZE;
    for (uint64_t i = s; i < e && i < pmm_total_pages; i++) {
        if (bitmap_is_free(i)) {
            bitmap_set_used(i);
            pmm_free_pages--;
            pmm_reserved_pages++;
        }
    }
}

/*------------------------------------------------------------------------*/
/* Public API                                                             */
/*------------------------------------------------------------------------*/
void pmm_init(uint64_t mem_top) {
    /* Clear bitmap (all-used by default). */
    for (uint64_t w = 0; w < PMM_BITMAP_WORDS; w++) {
        pmm_bitmap[w] = 0ULL;
    }

    pmm_total_pages    = mem_top / (uint64_t)PAGE_SIZE;
    if (pmm_total_pages > PMM_TOTAL_PAGES) {
        pmm_total_pages = PMM_TOTAL_PAGES;
    }
    pmm_free_pages     = 0ULL;
    pmm_reserved_pages = 0ULL;

    /* Initially mark every page in [0, mem_top) as free; reserved regions */
    /* will be carved out below.                                            */
    for (uint64_t i = 0; i < pmm_total_pages; i++) {
        bitmap_set_free(i);
        pmm_free_pages++;
    }

    /* BIOS / IVT / BDA / EBDA / Stage2 / VGA all live below 1 MiB. */
    mark_range_used(0ULL, 0x100000ULL);
    /* VGA explicitly (defensive, already covered above). */
    mark_range_used(0xB8000ULL, 0xC0000ULL);
    /* Kernel image: .text + .rodata + .data + .bss. */
    mark_range_used((uint64_t)(uintptr_t)_kernel_start,
                    (uint64_t)(uintptr_t)_kernel_end);
    /* Initial kernel heap window managed by heap.c. */
    mark_range_used((uint64_t)KERNEL_HEAP_START,
                    (uint64_t)KERNEL_HEAP_START + (uint64_t)KERNEL_HEAP_INITIAL_SIZE);
}

void *pmm_alloc_page(void) {
    for (uint64_t w = 0; w < PMM_BITMAP_WORDS; w++) {
        uint64_t word = pmm_bitmap[w];
        if (word == 0ULL) continue;

        /* Find first set bit (lowest free page in this word). */
        uint64_t bit = 0ULL;
        while ((word & 1ULL) == 0ULL) {
            word >>= 1U;
            bit++;
        }

        uint64_t page_idx = (w << 6U) + bit;
        if (page_idx >= pmm_total_pages) {
            return NULL;
        }

        bitmap_set_used(page_idx);
        pmm_free_pages--;
        return (void *)(uintptr_t)(page_idx * (uint64_t)PAGE_SIZE);
    }
    return NULL;
}

void pmm_free_page(void *phys_addr) {
    if (phys_addr == NULL) return;

    uint64_t addr = (uint64_t)(uintptr_t)phys_addr;
    if ((addr & ((uint64_t)PAGE_SIZE - 1ULL)) != 0ULL) return;

    uint64_t page_idx = addr / (uint64_t)PAGE_SIZE;
    if (page_idx >= pmm_total_pages) return;
    if (bitmap_is_free(page_idx))    return;  /* Double-free guard. */

    bitmap_set_free(page_idx);
    pmm_free_pages++;
}

uint64_t pmm_get_free_count(void) {
    return pmm_free_pages;
}

uint64_t pmm_get_used_count(void) {
    /* used = total - free - reserved */
    return pmm_total_pages - pmm_free_pages - pmm_reserved_pages;
}
