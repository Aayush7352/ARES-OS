#ifndef PROCESS_QUEUE_H
#define PROCESS_QUEUE_H

#include "pcb.h"

/* Simple intrusive FIFO queue for process control blocks */
typedef struct {
    pcb_t   *head;
    pcb_t   *tail;
    uint32_t count;
} process_queue_t;

/* Queue operations */
void  queue_init(process_queue_t *q);
void  queue_push(process_queue_t *q, pcb_t *proc);
pcb_t *queue_pop(process_queue_t *q);
void  queue_remove(process_queue_t *q, pcb_t *proc);
pcb_t *queue_peek(const process_queue_t *q);
int   queue_contains(const process_queue_t *q, const pcb_t *proc);

#endif /* PROCESS_QUEUE_H */
