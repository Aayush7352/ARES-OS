#ifndef ARES_VMM_H
#define ARES_VMM_H

#include "memory.h"

/*------------------------------------------------------------------------*/
/* Virtual Memory Manager                                                 */
/*                                                                        */
/* Operates on the 4-level page tables already set up by the bootloader.  */
/* The current CR3 is read at vmm_init() time and reused thereafter.      */
/*                                                                        */
/* IMPORTANT: every physical page referenced by these helpers must be     */
/* identity-mapped (the bootloader maps 0-16 MiB with 2 MiB pages, so as  */
/* long as page tables and the addresses being mapped live in that        */
/* window everything Just Works).                                         */
/*------------------------------------------------------------------------*/

/* Flag bits accepted by vmm_map_page(). */
#define VMM_READ   (1ULL << 0)
#define VMM_WRITE  (1ULL << 1)
#define VMM_EXEC   (1ULL << 2)
#define VMM_USER   (1ULL << 3)

void   vmm_init(void);

bool   vmm_map_page(void *virt_addr, void *phys_addr, uint64_t flags);
bool   vmm_unmap_page(void *virt_addr);
void  *vmm_query(void *virt_addr);
void   vmm_split_large_page(void *virt_addr);

#endif /* ARES_VMM_H */
