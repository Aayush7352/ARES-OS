#include "process.h"
#include "pcb.h"
#include "queue.h"
#include "scheduler.h"
#include "console.h"
#include "irq.h"
#include <stddef.h>

/* Global state */
pcb_t          *current_process = NULL;
uint32_t        next_pid = 1;
volatile uint32_t process_count = 0;

static process_queue_t blocked_queue;

/* Forward declaration */
static void idle_process(void);

void process_init(void) {
    pcb_init_table();
    queue_init(&blocked_queue);
    current_process = NULL;
    next_pid = 1;
    process_count = 0;

    console_writeline("[proc] Process manager initialized");
    console_printf("[proc] Max processes: %d, Stack size: %d\n",
                   MAX_PROCESSES, STACK_SIZE);

    /* Create the idle process */
    pcb_t *idle = process_create("idle", idle_process, PRIORITY_LOW);
    if (idle) {
        console_writeline("[proc] Idle process created");
    }
}

static void idle_process(void) {
    while (1) {
        __asm__ volatile("sti; hlt");
        /* Yield to let other processes run after timer wakes us */
        yield();
    }
}

pcb_t *process_create(const char *name, void (*entry)(void), uint32_t priority) {
    pcb_t *pcb = pcb_alloc();
    if (!pcb) {
        console_writeline("[proc] ERROR: No free PCB slot");
        return NULL;
    }

    pcb->pid = next_pid++;
    int i = 0;
    while (name[i] && i < PROCESS_NAME_MAX - 1) {
        pcb->name[i] = name[i];
        i++;
    }
    pcb->name[i] = '\0';
    pcb->priority = priority;
    pcb->state    = PROCESS_READY;
    pcb->cr3      = 0;  /* Will use kernel page table for now */
    pcb->time_created = 0;
    pcb->total_ticks  = 0;
    pcb->wake_tick    = 0;

    /* Set up initial stack frame for context switch.
     * Layout (from top-of-stack downward):
     *   [return to entry]
     *   [saved r15]
     *   [saved r14]
     *   [saved r13]
     *   [saved r12]
     *   [saved rbx]
     *   [saved rbp]
     * This matches the context_switch assembly restore sequence. */
    uint64_t *stack = (uint64_t *)pcb->stack_base;

    /* Push initial return address (entry point) */
    *(--stack) = (uint64_t)entry;

    /* Push callee-saved register save area (zeros for initial start) */
    *(--stack) = 0;  /* r15 */
    *(--stack) = 0;  /* r14 */
    *(--stack) = 0;  /* r13 */
    *(--stack) = 0;  /* r12 */
    *(--stack) = 0;  /* rbx */
    *(--stack) = 0;  /* rbp */

    /* RSP points to the saved rbp slot. context_switch will pop these
     * registers and then 'ret' jumps to the entry point. */
    pcb->rsp = (uint64_t)stack;

    /* Add to scheduler's ready queue */
    scheduler_add(pcb);
    process_count++;

    return pcb;
}

void process_exit(int exit_code) {
    (void)exit_code;
    if (!current_process) return;

    console_printf("[proc] Process %d (%s) exiting\n",
                   current_process->pid, current_process->name);

    current_process->state = PROCESS_TERMINATED;
    process_count--;
    pcb_t *dead = current_process;

    /* Pick next process from scheduler */
    pcb_t *next = scheduler_next();
    if (!next) {
        /* Should not happen - idle process should always be ready */
        console_writeline("[proc] PANIC: No process to switch to!");
        for (;;) { __asm__ volatile("cli; hlt"); }
    }

    next->state = PROCESS_RUNNING;
    current_process = next;
    pcb_free(dead);
    context_switch(next);
    /* Never reaches here */
}

pcb_t *process_by_pid(uint32_t pid) {
    if (current_process && current_process->pid == pid)
        return current_process;
    return pcb_by_pid(pid);
}

int process_kill(uint32_t pid) {
    if (pid == 1) {
        console_writeline("[proc] Cannot kill idle process");
        return -1;
    }
    if (current_process && current_process->pid == pid) {
        process_exit(0);
        return 0;
    }
    pcb_t *proc = pcb_by_pid(pid);
    if (!proc) return -1;

    scheduler_remove(proc);
    queue_remove(&blocked_queue, proc);
    proc->state = PROCESS_TERMINATED;
    process_count--;
    pcb_free(proc);
    return 0;
}

void process_ps(void) {
    console_printf("%-4s %-24s %-10s %-8s %-8s %s\n",
                   "PID", "NAME", "STATE", "PRIO", "TICKS", "TOTAL");
    console_printf("%-4s %-24s %-10s %-8s %-8s %s\n",
                   "---", "----", "-----", "----", "-----", "-----");

    if (current_process) {
        console_printf("%-4u %-24s %-10s %-8s %-8u (current)\n",
                       current_process->pid,
                       current_process->name,
                       process_state_str(current_process->state),
                       process_priority_str(current_process->priority),
                       0, current_process->total_ticks);
    }

    pcb_t *buf[64];
    int count = pcb_enum_active(buf, 64);
    for (int i = 0; i < count; i++) {
        if (buf[i] == current_process) continue;
        console_printf("%-4u %-24s %-10s %-8s %-8u\n",
                       buf[i]->pid, buf[i]->name,
                       process_state_str(buf[i]->state),
                       process_priority_str(buf[i]->priority),
                       buf[i]->total_ticks);
    }
}

void yield(void) {
    if (!current_process) return;

    /* Re-queue current process into scheduler */
    current_process->state = PROCESS_READY;
    scheduler_add(current_process);

    /* Get next process from the active scheduling algorithm */
    pcb_t *next = scheduler_next();
    if (!next) {
        /* No other process — remove current from scheduler and return */
        scheduler_remove(current_process);
        return;
    }

    /* Switch to next process */
    next->state = PROCESS_RUNNING;
    pcb_t *prev = current_process;
    current_process = next;

    context_switch(next);
    /* Execution resumes here when this process is switched back to */
    (void)prev;
}
