#ifndef ARES_MEMORY_H
#define ARES_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*------------------------------------------------------------------------*/
/* Page geometry                                                          */
/*------------------------------------------------------------------------*/
#define PAGE_SIZE        4096U
#define PAGE_SHIFT       12U
#define LARGE_PAGE_SIZE  0x200000U          /* 2 MiB */

/*------------------------------------------------------------------------*/
/* Kernel heap layout                                                     */
/*------------------------------------------------------------------------*/
#define KERNEL_HEAP_START         0x200000UL   /* After kernel image (2 MiB) */
#define KERNEL_HEAP_INITIAL_SIZE  0x400000UL   /* 4 MiB initial heap         */

/*------------------------------------------------------------------------*/
/* Physical RAM bounds                                                    */
/*------------------------------------------------------------------------*/
#define MEM_PHYSICAL_START  0x100000UL    /* Where usable RAM starts (1 MiB) */
#define MEM_PHYSICAL_END    0x4000000UL   /* 64 MiB upper bound              */

#endif /* ARES_MEMORY_H */
