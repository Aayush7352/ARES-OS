#include "vmm.h"
#include "pmm.h"

/*------------------------------------------------------------------------*/
/* x86_64 page-table entry bits                                           */
/*------------------------------------------------------------------------*/
#define PT_PRESENT    (1ULL << 0)
#define PT_WRITABLE   (1ULL << 1)
#define PT_USER       (1ULL << 2)
#define PT_HUGE       (1ULL << 7)

/* Bits 12..51 = physical address of next-level table or page frame.      */
#define PT_PHYS_MASK  0x000FFFFFFFFFF000ULL

#define PML4_INDEX(va) (((va) >> 39U) & 0x1FFULL)
#define PDPT_INDEX(va) (((va) >> 30U) & 0x1FFULL)
#define PD_INDEX(va)   (((va) >> 21U) & 0x1FFULL)
#define PT_INDEX(va)   (((va) >> 12U) & 0x1FFULL)

#define PT_ENTRIES     512U

/*------------------------------------------------------------------------*/
/* State                                                                  */
/*------------------------------------------------------------------------*/
static uint64_t vmm_cr3 = 0ULL;

/*------------------------------------------------------------------------*/
/* Hardware helpers                                                       */
/*------------------------------------------------------------------------*/
static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void invlpg(uint64_t va) {
    __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
}

/* Convert public VMM_* flags into a PTE-flag bitmask (PRESENT always set). */
/* EXEC is implied (NX bit omitted because EFER.NXE may not be enabled).    */
static inline uint64_t pte_flags_from(uint64_t flags) {
    uint64_t out = PT_PRESENT;
    if ((flags & VMM_WRITE) != 0ULL) out |= PT_WRITABLE;
    if ((flags & VMM_USER)  != 0ULL) out |= PT_USER;
    return out;
}

/*------------------------------------------------------------------------*/
/* Page-table descent helper.                                             */
/*                                                                        */
/* If parent[idx] is present, returns the address of the next-level table.*/
/* Otherwise (when create is true) allocates a fresh zero-filled page from*/
/* the PMM, installs it with `intermediate_flags`, and returns it.        */
/* Returns NULL on OOM or when the entry is absent and create is false.   */
/*------------------------------------------------------------------------*/
static uint64_t *get_or_create_table(uint64_t *parent,
                                     uint64_t  idx,
                                     uint64_t  intermediate_flags,
                                     bool      create) {
    uint64_t entry = parent[idx];
    if ((entry & PT_PRESENT) != 0ULL) {
        return (uint64_t *)(uintptr_t)(entry & PT_PHYS_MASK);
    }
    if (!create) return NULL;

    void *page = pmm_alloc_page();
    if (page == NULL) return NULL;

    uint64_t *table = (uint64_t *)page;
    for (uint32_t i = 0; i < PT_ENTRIES; i++) {
        table[i] = 0ULL;
    }
    parent[idx] = ((uint64_t)(uintptr_t)page & PT_PHYS_MASK) | intermediate_flags;
    return table;
}

/*------------------------------------------------------------------------*/
/* Public API                                                             */
/*------------------------------------------------------------------------*/
void vmm_init(void) {
    vmm_cr3 = read_cr3();
}

bool vmm_map_page(void *virt_addr, void *phys_addr, uint64_t flags) {
    if (vmm_cr3 == 0ULL) vmm_cr3 = read_cr3();

    uint64_t va = (uint64_t)(uintptr_t)virt_addr;
    uint64_t pa = (uint64_t)(uintptr_t)phys_addr;

    if ((va & ((uint64_t)PAGE_SIZE - 1ULL)) != 0ULL) return false;
    if ((pa & ((uint64_t)PAGE_SIZE - 1ULL)) != 0ULL) return false;

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(vmm_cr3 & PT_PHYS_MASK);

    /* Intermediate tables are always RW; USER propagates from caller.    */
    uint64_t inter = PT_PRESENT | PT_WRITABLE;
    if ((flags & VMM_USER) != 0ULL) inter |= PT_USER;

    uint64_t *pdpt = get_or_create_table(pml4, PML4_INDEX(va), inter, true);
    if (pdpt == NULL) return false;

    uint64_t *pd = get_or_create_table(pdpt, PDPT_INDEX(va), inter, true);
    if (pd == NULL) return false;

    /* If the PD slot is a 2 MiB huge page, split it before installing a  */
    /* 4 KiB mapping inside its range.                                     */
    uint64_t pde = pd[PD_INDEX(va)];
    if ((pde & PT_PRESENT) != 0ULL && (pde & PT_HUGE) != 0ULL) {
        vmm_split_large_page(virt_addr);
    }

    uint64_t *pt = get_or_create_table(pd, PD_INDEX(va), inter, true);
    if (pt == NULL) return false;

    pt[PT_INDEX(va)] = (pa & PT_PHYS_MASK) | pte_flags_from(flags);
    invlpg(va);
    return true;
}

bool vmm_unmap_page(void *virt_addr) {
    if (vmm_cr3 == 0ULL) vmm_cr3 = read_cr3();

    uint64_t va = (uint64_t)(uintptr_t)virt_addr;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(vmm_cr3 & PT_PHYS_MASK);

    uint64_t pml4e = pml4[PML4_INDEX(va)];
    if ((pml4e & PT_PRESENT) == 0ULL) return false;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & PT_PHYS_MASK);

    uint64_t pdpte = pdpt[PDPT_INDEX(va)];
    if ((pdpte & PT_PRESENT) == 0ULL) return false;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & PT_PHYS_MASK);

    uint64_t pde = pd[PD_INDEX(va)];
    if ((pde & PT_PRESENT) == 0ULL) return false;

    /* Unmapping the whole 2 MiB region: drop the PD entry directly.      */
    if ((pde & PT_HUGE) != 0ULL) {
        pd[PD_INDEX(va)] = 0ULL;
        invlpg(va);
        return true;
    }

    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & PT_PHYS_MASK);
    if ((pt[PT_INDEX(va)] & PT_PRESENT) == 0ULL) return false;

    pt[PT_INDEX(va)] = 0ULL;
    invlpg(va);
    return true;
}

void *vmm_query(void *virt_addr) {
    if (vmm_cr3 == 0ULL) vmm_cr3 = read_cr3();

    uint64_t va = (uint64_t)(uintptr_t)virt_addr;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(vmm_cr3 & PT_PHYS_MASK);

    uint64_t pml4e = pml4[PML4_INDEX(va)];
    if ((pml4e & PT_PRESENT) == 0ULL) return NULL;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & PT_PHYS_MASK);

    uint64_t pdpte = pdpt[PDPT_INDEX(va)];
    if ((pdpte & PT_PRESENT) == 0ULL) return NULL;
    /* 1 GiB huge page. */
    if ((pdpte & PT_HUGE) != 0ULL) {
        uint64_t off = va & ((1ULL << 30U) - 1ULL);
        return (void *)(uintptr_t)((pdpte & PT_PHYS_MASK) | off);
    }
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & PT_PHYS_MASK);

    uint64_t pde = pd[PD_INDEX(va)];
    if ((pde & PT_PRESENT) == 0ULL) return NULL;
    /* 2 MiB huge page. */
    if ((pde & PT_HUGE) != 0ULL) {
        uint64_t off = va & ((uint64_t)LARGE_PAGE_SIZE - 1ULL);
        return (void *)(uintptr_t)((pde & PT_PHYS_MASK) | off);
    }
    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & PT_PHYS_MASK);

    uint64_t pte = pt[PT_INDEX(va)];
    if ((pte & PT_PRESENT) == 0ULL) return NULL;

    uint64_t off = va & ((uint64_t)PAGE_SIZE - 1ULL);
    return (void *)(uintptr_t)((pte & PT_PHYS_MASK) | off);
}

/*------------------------------------------------------------------------*/
/* Split a 2 MiB PD entry into 512 4 KiB PT entries.                      */
/*                                                                        */
/* We allocate a fresh page-table page from the PMM, populate all 512     */
/* entries to cover the original 2 MiB physical range with the same flags */
/* (minus the HUGE bit), then atomically swap the PD slot. Finally invlpg */
/* across the 2 MiB window so the TLB drops every stale large entry.      */
/*------------------------------------------------------------------------*/
void vmm_split_large_page(void *virt_addr) {
    if (vmm_cr3 == 0ULL) vmm_cr3 = read_cr3();

    uint64_t va = (uint64_t)(uintptr_t)virt_addr;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(vmm_cr3 & PT_PHYS_MASK);

    uint64_t pml4e = pml4[PML4_INDEX(va)];
    if ((pml4e & PT_PRESENT) == 0ULL) return;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & PT_PHYS_MASK);

    uint64_t pdpte = pdpt[PDPT_INDEX(va)];
    if ((pdpte & PT_PRESENT) == 0ULL) return;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & PT_PHYS_MASK);

    uint64_t pde = pd[PD_INDEX(va)];
    if ((pde & PT_PRESENT) == 0ULL || (pde & PT_HUGE) == 0ULL) return;

    void *new_pt = pmm_alloc_page();
    if (new_pt == NULL) return;

    uint64_t *pt = (uint64_t *)new_pt;
    uint64_t base_phys  = pde & PT_PHYS_MASK;
    /* Carry through the original lower-12 flag bits except HUGE. */
    uint64_t base_flags = (pde & 0xFFFULL) & ~PT_HUGE;

    for (uint32_t i = 0; i < PT_ENTRIES; i++) {
        pt[i] = (base_phys + ((uint64_t)i * (uint64_t)PAGE_SIZE)) | base_flags;
    }

    /* PD slot now points to the new PT (intermediate flags only). */
    uint64_t new_pd = ((uint64_t)(uintptr_t)new_pt & PT_PHYS_MASK)
                    | PT_PRESENT | PT_WRITABLE | (pde & PT_USER);
    pd[PD_INDEX(va)] = new_pd;

    /* Flush every 4 KiB slot within the original 2 MiB window. */
    uint64_t base_va = va & ~((uint64_t)LARGE_PAGE_SIZE - 1ULL);
    for (uint32_t i = 0; i < PT_ENTRIES; i++) {
        invlpg(base_va + ((uint64_t)i * (uint64_t)PAGE_SIZE));
    }
}
