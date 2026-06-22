#include "queue.h"
#include <stddef.h>

void queue_init(process_queue_t *q) {
    q->head  = NULL;
    q->tail  = NULL;
    q->count = 0;
}

void queue_push(process_queue_t *q, pcb_t *proc) {
    proc->next = NULL;
    if (q->tail) {
        q->tail->next = proc;
    } else {
        q->head = proc;
    }
    q->tail = proc;
    q->count++;
}

pcb_t *queue_pop(process_queue_t *q) {
    if (!q->head) return NULL;
    pcb_t *proc = q->head;
    q->head = proc->next;
    if (!q->head) q->tail = NULL;
    proc->next = NULL;
    q->count--;
    return proc;
}

void queue_remove(process_queue_t *q, pcb_t *proc) {
    pcb_t *prev = NULL;
    pcb_t *curr = q->head;
    while (curr) {
        if (curr == proc) {
            if (prev) prev->next = curr->next;
            else      q->head = curr->next;
            if (!curr->next) q->tail = prev;
            curr->next = NULL;
            q->count--;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

pcb_t *queue_peek(const process_queue_t *q) {
    return q->head;
}

int queue_contains(const process_queue_t *q, const pcb_t *proc) {
    pcb_t *curr = q->head;
    while (curr) {
        if (curr == proc) return 1;
        curr = curr->next;
    }
    return 0;
}
