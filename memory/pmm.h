#ifndef ARES_PMM_H
#define ARES_PMM_H

#include "memory.h"

/*------------------------------------------------------------------------*/
/* Physical Memory Manager                                                */
/*                                                                        */
/* Bitmap-based 4 KiB page allocator. Bit = 1 means free, bit = 0 means   */
/* used. pmm_init() reserves the BIOS region, the kernel image, VGA       */
/* framebuffer and the kernel heap window so they are never handed out.   */
/*------------------------------------------------------------------------*/

/* Initialize the PMM. mem_top is the upper bound (exclusive) of usable  */
/* physical RAM in bytes (e.g. 0x4000000 for 64 MiB).                     */
void     pmm_init(uint64_t mem_top);

/* Allocate one 4 KiB page. Returns physical address or NULL on OOM.     */
void    *pmm_alloc_page(void);

/* Release a page previously returned by pmm_alloc_page().               */
void     pmm_free_page(void *phys_addr);

/* Statistics. */
uint64_t pmm_get_free_count(void);
uint64_t pmm_get_used_count(void);

#endif /* ARES_PMM_H */
