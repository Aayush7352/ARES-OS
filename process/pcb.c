#include "pcb.h"
#include <stddef.h>

/* Global process table */
static pcb_t pcb_table[MAX_PROCESSES];
static uint8_t process_stacks[MAX_PROCESSES][STACK_SIZE]
    __attribute__((aligned(16)));

/* Bitmap for free PCB slots (1 = free) */
static uint64_t slot_free_map[1];  /* 64-bit bitmap enough for 64 procs */

void pcb_init_table(void) {
    slot_free_map[0] = ~0ULL;  /* All slots free initially */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_table[i].pid  = 0;
        pcb_table[i].state = PROCESS_TERMINATED;
        pcb_table[i].next  = NULL;
    }
}

static int find_free_slot(void) {
    if (slot_free_map[0] == 0) return -1;
    unsigned long idx = 0;
    /* Find first set bit */
    uint64_t m = slot_free_map[0];
    while ((m & 1) == 0) { m >>= 1; idx++; }
    return (int)idx;
}

static void mark_slot_used(int idx) {
    slot_free_map[0] &= ~(1ULL << idx);
}

static void mark_slot_free(int idx) {
    slot_free_map[0] |= (1ULL << idx);
}

pcb_t *pcb_alloc(void) {
    int idx = find_free_slot();
    if (idx < 0) return NULL;
    mark_slot_used(idx);
    pcb_t *p = &pcb_table[idx];
    p->pid         = 0;
    p->state       = PROCESS_EMBRYO;
    p->priority    = PRIORITY_NORMAL;
    p->rsp         = 0;
    p->rip         = 0;
    p->rflags      = 0;
    p->cr3         = 0;
    p->stack_base  = (uint64_t)process_stacks[idx] + STACK_SIZE;
    p->time_created = 0;
    p->total_ticks = 0;
    p->wake_tick   = 0;
    p->next        = NULL;
    return p;
}

void pcb_free(pcb_t *pcb) {
    if (!pcb) return;
    int idx = (int)(pcb - pcb_table);
    if (idx < 0 || idx >= MAX_PROCESSES) return;
    pcb->state = PROCESS_TERMINATED;
    mark_slot_free(idx);
}

pcb_t *pcb_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (pcb_table[i].pid == pid &&
            pcb_table[i].state != PROCESS_EMBRYO &&
            pcb_table[i].state != PROCESS_TERMINATED)
            return &pcb_table[i];
    }
    return NULL;
}

int pcb_enum_active(pcb_t **buf, int max) {
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES && count < max; i++) {
        if (pcb_table[i].state != PROCESS_EMBRYO &&
            pcb_table[i].state != PROCESS_TERMINATED) {
            buf[count++] = &pcb_table[i];
        }
    }
    return count;
}
