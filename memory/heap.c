#include "heap.h"

/*------------------------------------------------------------------------*/
/* Heap configuration                                                     */
/*------------------------------------------------------------------------*/
#define HEAP_MAGIC  0xA55A5A55U
#define HEAP_ALIGN  16U

/*------------------------------------------------------------------------*/
/* Block header                                                           */
/*                                                                        */
/* `size` is the payload size in bytes (excluding this header). Headers   */
/* form a singly linked list ordered by address; coalescing is forward-   */
/* only on free.                                                          */
/*------------------------------------------------------------------------*/
struct heap_block {
    size_t              size;
    uint32_t            magic;
    struct heap_block  *next;
    int                 free;
};

static struct heap_block *heap_head = NULL;

/*------------------------------------------------------------------------*/
/* Helpers                                                                */
/*------------------------------------------------------------------------*/
static inline size_t align_up(size_t n, size_t a) {
    return (n + a - 1U) & ~(a - 1U);
}

static inline struct heap_block *block_from_payload(void *ptr) {
    /* Casting via uintptr_t silences -Wcast-align since the caller is    */
    /* responsible for the alignment invariant.                            */
    uintptr_t addr = (uintptr_t)ptr - sizeof(struct heap_block);
    return (struct heap_block *)addr;
}

static inline void *payload_of(struct heap_block *b) {
    uintptr_t addr = (uintptr_t)b + sizeof(struct heap_block);
    return (void *)addr;
}

/*------------------------------------------------------------------------*/
/* Public API                                                             */
/*------------------------------------------------------------------------*/
void heap_init(void) {
    heap_head = (struct heap_block *)(uintptr_t)KERNEL_HEAP_START;
    heap_head->size  = (size_t)KERNEL_HEAP_INITIAL_SIZE - sizeof(struct heap_block);
    heap_head->magic = HEAP_MAGIC;
    heap_head->next  = NULL;
    heap_head->free  = 1;
}

void *kmalloc(size_t size) {
    if (size == 0U || heap_head == NULL) return NULL;

    size_t want = align_up(size, (size_t)HEAP_ALIGN);

    for (struct heap_block *b = heap_head; b != NULL; b = b->next) {
        if (b->magic != HEAP_MAGIC) return NULL;        /* Corruption.   */
        if (b->free == 0 || b->size < want)  continue;

        /* Split off the tail if there is room for a real follow-up block. */
        size_t remainder = b->size - want;
        if (remainder > sizeof(struct heap_block) + (size_t)HEAP_ALIGN) {
            uintptr_t tail_addr = (uintptr_t)payload_of(b) + want;
            struct heap_block *tail = (struct heap_block *)tail_addr;
            tail->size  = remainder - sizeof(struct heap_block);
            tail->magic = HEAP_MAGIC;
            tail->next  = b->next;
            tail->free  = 1;

            b->size = want;
            b->next = tail;
        }

        b->free = 0;
        return payload_of(b);
    }
    return NULL;
}

void kfree(void *ptr) {
    if (ptr == NULL) return;

    struct heap_block *b = block_from_payload(ptr);
    if (b->magic != HEAP_MAGIC) return;
    if (b->free != 0)           return;          /* Double-free guard.    */
    b->free = 1;

    /* Forward coalesce with adjacent free neighbours. */
    while (b->next != NULL
        && b->next->free != 0
        && b->next->magic == HEAP_MAGIC) {
        b->size += sizeof(struct heap_block) + b->next->size;
        b->next  = b->next->next;
    }
}

void *krealloc(void *ptr, size_t new_size) {
    if (ptr == NULL)       return kmalloc(new_size);
    if (new_size == 0U)  { kfree(ptr); return NULL; }

    struct heap_block *b = block_from_payload(ptr);
    if (b->magic != HEAP_MAGIC) return NULL;

    if (b->size >= new_size) return ptr;

    void *fresh = kmalloc(new_size);
    if (fresh == NULL) return NULL;

    const uint8_t *src = (const uint8_t *)ptr;
    uint8_t       *dst = (uint8_t *)fresh;
    for (size_t i = 0; i < b->size; i++) {
        dst[i] = src[i];
    }
    kfree(ptr);
    return fresh;
}

void heap_stats(uint64_t *used, uint64_t *free, uint64_t *largest) {
    uint64_t u = 0ULL;
    uint64_t f = 0ULL;
    uint64_t l = 0ULL;

    for (struct heap_block *b = heap_head; b != NULL; b = b->next) {
        if (b->magic != HEAP_MAGIC) break;
        if (b->free != 0) {
            f += (uint64_t)b->size;
            if ((uint64_t)b->size > l) l = (uint64_t)b->size;
        } else {
            u += (uint64_t)b->size;
        }
    }

    if (used    != NULL) *used    = u;
    if (free    != NULL) *free    = f;
    if (largest != NULL) *largest = l;
}
