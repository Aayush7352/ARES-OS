# ARES OS, Memory Management

Memory in ARES is split into three layers, each with a clear job. The physical memory manager (PMM) hands out 4 KB pages. The virtual memory manager (VMM) builds page tables and maps pages into address spaces. The heap carves arbitrary-sized chunks out of pages for the kernel's own use. Nothing else allocates memory directly.

## Physical Memory Manager

The PMM tracks 64 MB of RAM with a bitmap. One bit per 4 KB page, so 16,384 bits, which is exactly 2 KB of bitmap. We round up to a full page so the bitmap itself can live in the PMM-managed region without a chicken-and-egg problem.

```c
#define PMM_PAGE_SIZE   4096
#define PMM_TOTAL_PAGES 16384            /* 64 MB / 4 KB */
#define PMM_BITMAP_SIZE (PMM_TOTAL_PAGES / 8)   /* 2048 bytes */

static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

void   pmm_init(boot_info_t* info);
void*  pmm_alloc_page(void);
void   pmm_free_page(void* page);
size_t pmm_free_count(void);
```

A set bit means the page is in use. `pmm_init` marks everything below `0x110000` as used (kernel image, stack, BSS, the bitmap itself, the boot-time page tables). Everything from `0x110000` to the end of RAM starts free.

`pmm_alloc_page` is a linear scan with a remembered cursor. Allocation is O(n) worst case but O(1) in practice because the cursor advances and wraps. Free pages don't have to come back in order, but the scan picks the lowest-numbered free page, which keeps fragmentation predictable.

```c
void* pmm_alloc_page(void) {
    for (size_t i = 0; i < PMM_TOTAL_PAGES; i++) {
        size_t idx = (pmm_cursor + i) % PMM_TOTAL_PAGES;
        if (!(pmm_bitmap[idx >> 3] & (1 << (idx & 7)))) {
            pmm_bitmap[idx >> 3] |= (1 << (idx & 7));
            pmm_cursor = (idx + 1) % PMM_TOTAL_PAGES;
            return (void*)(idx * PMM_PAGE_SIZE);
        }
    }
    return NULL;   /* ARES_ENOMEM */
}
```

We don't zero pages on alloc. Callers that need zeroed memory do it themselves. This matches the kernel-only contract: there's no userspace yet, so there's no information-leak threat.

## Virtual Memory Manager

x86_64 long mode uses 4-level page tables: PML4, PDPT, PD, PT, each with 512 entries of 8 bytes. A virtual address is sliced into 9-bit indices plus a 12-bit page offset.

```text
 63        47   38   29   20   11        0
  +---------+----+----+----+----+----------+
  |  sign   |PML4|PDPT| PD | PT |  offset  |
  +---------+----+----+----+----+----------+
```

`vmm_init` adopts the boot-time PML4 at `0x1000` and starts allocating new tables from the PMM. Mapping a page walks the four levels, creating intermediate tables on demand.

```c
typedef uint64_t pte_t;

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_NX       (1ULL << 63)

ares_status_t vmm_map(uint64_t va, uint64_t pa, uint64_t flags);
ares_status_t vmm_unmap(uint64_t va);
uint64_t       vmm_translate(uint64_t va);
void           vmm_flush(uint64_t va);   /* invlpg */
```

The map routine is the workhorse. Each level either finds an existing table or asks the PMM for a fresh page, zeros it, and writes the parent entry. The final PTE gets the requested flags ORed with `PTE_PRESENT`.

```c
ares_status_t vmm_map(uint64_t va, uint64_t pa, uint64_t flags) {
    pte_t* pml4 = current_pml4;
    pte_t* pdpt = ensure_table(&pml4[(va >> 39) & 0x1FF]);
    pte_t* pd   = ensure_table(&pdpt[(va >> 30) & 0x1FF]);
    pte_t* pt   = ensure_table(&pd[(va >> 21) & 0x1FF]);
    pt[(va >> 12) & 0x1FF] = (pa & ~0xFFFULL) | flags | PTE_PRESENT;
    vmm_flush(va);
    return ARES_OK;
}
```

We always `invlpg` after a map or unmap. Forgetting that flush is the single most painful bug in this kernel's history, because it works on the first run and fails on the second when the TLB still holds the stale entry.

## Heap

The heap sits on top of the PMM. It serves the kernel's `kmalloc` and `kfree`. The strategy is first-fit with a doubly-linked free list and a one-word header per block.

```c
typedef struct heap_block {
    size_t size;              /* includes header */
    struct heap_block* next;
    struct heap_block* prev;
    uint32_t magic;           /* 0xA1E5A1E5 */
    uint8_t  free;
} heap_block_t;

void* kmalloc(size_t n);
void  kfree(void* p);
size_t heap_used(void);
```

`heap_init` requests a contiguous run of pages from the PMM and turns it into one giant free block. `kmalloc` walks the list, picks the first block that fits, splits it if the remainder is at least 32 bytes plus the header, and flips `free = 0`.

`kfree` coalesces eagerly. It looks at the previous and next blocks in address order and merges any neighbor that's also free. The doubly-linked list makes that O(1).

The `magic` field is a deliberate luxury. We pay 4 bytes per block so that `kfree` of a wild pointer panics with a clear message instead of corrupting the list silently. In a kernel with no userspace pressure, that trade is cheap.

## Init Sequence

The three layers come up in strict order during `kernel_main`:

```c
pmm_init(info);     /* bitmap from boot_info memory map */
vmm_init();         /* take over page tables from stage2 */
heap_init();        /* request initial pool, build free list */
```

After this, every allocation in the kernel goes through `kmalloc`. The PMM is reserved for "I need a whole aligned page" callers like the VMM itself, the PCB pool's stack pages, and the disk cache. We enforce this by code review, not by language, but the pattern is clean enough that violations stand out.

## Failure Modes

Out-of-memory is treated as a hard error. `pmm_alloc_page` returning NULL propagates as `ARES_ENOMEM` and the caller decides what to do. The shell prints an error. The scheduler refuses to create the process. The kernel itself panics only if init-time allocations fail, because there's no path forward from that.

There is no swap, no overcommit, no OOM killer. The kernel knows exactly how much RAM it has and refuses requests it can't satisfy. That makes the failure modes legible, which matters more in a teaching OS than squeezing the last megabyte.
