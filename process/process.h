#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "pcb.h"

/* Global process table and scheduler state */
extern pcb_t          *current_process;
extern uint32_t        next_pid;
extern volatile uint32_t process_count;

/* Process management API */
void      process_init(void);
pcb_t    *process_create(const char *name, void (*entry)(void), uint32_t priority);
void      process_exit(int exit_code);
pcb_t    *process_by_pid(uint32_t pid);
int       process_kill(uint32_t pid);
void      process_ps(void);

/* Context switch (implemented in assembly) */
void      context_switch(pcb_t *next);

/* Yield CPU to next ready process */
void      yield(void);

#endif /* PROCESS_H */
