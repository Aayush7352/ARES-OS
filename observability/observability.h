#ifndef ARES_OBSERVABILITY_H
#define ARES_OBSERVABILITY_H

#include <stdint.h>
#include <stddef.h>

/* Kernel log ring buffer */
#define LOG_BUF_SIZE   4096
#define LOG_MAX_MSG    128

void log_write(const char *msg);
const char *log_read(size_t *len_out);

/* System metrics */
typedef struct {
    uint64_t uptime_ticks;
    uint64_t ctx_switches;
    uint64_t interrupts_served;
    uint64_t heap_used;
    uint64_t heap_free;
    uint64_t mem_used_pages;
    uint64_t mem_free_pages;
    uint32_t process_count;
    uint32_t active_fds;
} system_metrics_t;

void metrics_collect(system_metrics_t *m);

/* Top-like display */
void top_display(void);

#endif
