#ifndef ARES_HEAP_H
#define ARES_HEAP_H

#include "memory.h"

/*------------------------------------------------------------------------*/
/* Kernel Heap                                                            */
/*                                                                        */
/* A simple first-fit linked-list allocator backed by a fixed 4 MiB       */
/* window starting at KERNEL_HEAP_START. The PMM reserves that range so   */
/* it never collides with general physical allocations. Headers are       */
/* magic-tagged to catch double-frees and corruption.                     */
/*------------------------------------------------------------------------*/

void   heap_init(void);

void  *kmalloc(size_t size);
void   kfree(void *ptr);
void  *krealloc(void *ptr, size_t new_size);

void   heap_stats(uint64_t *used, uint64_t *free, uint64_t *largest);

#endif /* ARES_HEAP_H */
