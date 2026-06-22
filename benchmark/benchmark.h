#ifndef ARES_BENCHMARK_H
#define ARES_BENCHMARK_H

#include <stdint.h>
#include <stddef.h>

/* Run scheduler benchmarks with 3 variants. Prints results to console. */
void bench_scheduler(void);

/* Run filesystem benchmarks (write throughput, read throughput). */
void bench_filesystem(void);

/* Run memory benchmarks (malloc/free throughput). */
void bench_memory(void);

/* Run all benchmarks and print summary. */
void bench_run_all(void);

#endif
