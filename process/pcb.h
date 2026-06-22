#ifndef PCB_H
#define PCB_H

#include <stdint.h>

#define MAX_PROCESSES   64
#define PROCESS_NAME_MAX 24
#define STACK_SIZE      4096

/* Process states */
enum process_state {
    PROCESS_EMBRYO    = 0,
    PROCESS_READY     = 1,
    PROCESS_RUNNING   = 2,
    PROCESS_BLOCKED   = 3,
    PROCESS_TERMINATED = 4
};

/* Process priorities */
enum process_priority {
    PRIORITY_LOW      = 0,
    PRIORITY_NORMAL   = 1,
    PRIORITY_HIGH     = 2,
    PRIORITY_REALTIME = 3,
    PRIORITY_COUNT    = 4
};

/* Process Control Block */
typedef struct pcb {
    uint32_t               pid;
    volatile enum process_state state;
    char                   name[PROCESS_NAME_MAX];
    uint32_t               priority;
    uint64_t               rsp;          /* Kernel stack pointer */
    uint64_t               rip;          /* Instruction pointer */
    uint64_t               rflags;       /* Saved RFLAGS */
    uint64_t               cr3;          /* Page table base */
    uint64_t               stack_base;   /* Bottom of kernel stack */
    uint32_t               time_created; /* Tick when created */
    uint32_t               total_ticks;  /* Total CPU time consumed */
    uint32_t               wake_tick;    /* For sleep/block with timeout */
    struct pcb            *next;         /* Queue linking */
} pcb_t;

/* PCB table management */
void   pcb_init_table(void);
pcb_t *pcb_alloc(void);
void   pcb_free(pcb_t *pcb);
pcb_t *pcb_by_pid(uint32_t pid);
int    pcb_enum_active(pcb_t **buf, int max);

/* Accessor macros for process state strings */
static inline const char *process_state_str(enum process_state s) {
    switch (s) {
        case PROCESS_EMBRYO:     return "EMBRYO";
        case PROCESS_READY:      return "READY";
        case PROCESS_RUNNING:    return "RUNNING";
        case PROCESS_BLOCKED:    return "BLOCKED";
        case PROCESS_TERMINATED: return "TERM";
        default:                 return "UNKNOWN";
    }
}

static inline const char *process_priority_str(enum process_priority p) {
    switch (p) {
        case PRIORITY_LOW:      return "LOW";
        case PRIORITY_NORMAL:   return "NORMAL";
        case PRIORITY_HIGH:     return "HIGH";
        case PRIORITY_REALTIME: return "RT";
        default:                return "?";
    }
}

#endif /* PCB_H */
