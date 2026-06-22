# ARES OS, Scheduler

The scheduler picks which process runs next. ARES ships four policies and lets you swap between them at boot or with a shell command. They all live behind the same interface, so the dispatcher doesn't know or care which one is active.

## The Interface

Every policy implements the same vtable. This is the only place in the kernel where we use a function-pointer struct, because the alternative is a chain of `if (policy == ...)` branches in the hot path.

```c
typedef struct scheduler_ops {
    const char* name;
    void  (*init)(void);
    void  (*add)(pcb_t* p);
    pcb_t* (*next)(void);
    void  (*remove)(pcb_t* p);
    void  (*tick)(pcb_t* current);
    void  (*get_stats)(sched_stats_t* out);
} scheduler_ops_t;

extern scheduler_ops_t* current_sched;

void scheduler_init(sched_policy_t p);
void scheduler_set(sched_policy_t p);   /* runtime swap */
```

`current_sched` is a global pointer. The timer ISR and the dispatcher both go through it. Swapping policies means draining the old one's queue, copying the ready list into the new one, and atomically updating the pointer with interrupts off.

## The need_resched Flag

The scheduler doesn't run on every interrupt. It runs when somebody sets `need_resched = 1` and we're about to return to user mode (or to the idle loop). This keeps the hot path of irrelevant IRQs cheap.

```c
volatile int need_resched;

void schedule(void) {
    if (!need_resched) return;
    need_resched = 0;

    pcb_t* prev = current;
    pcb_t* next = current_sched->next();
    if (next && next != prev) {
        context_switch(&prev->rsp, next->rsp);
    }
}
```

`tick()` is what sets the flag. For preemptive policies it decrements a quantum and flips the flag when it hits zero. For FCFS, it does nothing.

## FCFS

First-come, first-served. A single linked list, no preemption. Once a process gets the CPU, it keeps it until it blocks or exits. Useful as a baseline and for batch-style tests.

```c
static pcb_t* fcfs_head;
static pcb_t* fcfs_tail;

void fcfs_add(pcb_t* p)   { /* append to tail */ }
pcb_t* fcfs_next(void)    { return fcfs_head; }   /* head stays */
void fcfs_tick(pcb_t* c)  { /* no preemption */ }
```

## Round Robin

The default. Quantum is 3 timer ticks. When a quantum expires, the current process goes to the tail and we set `need_resched`.

```c
#define RR_QUANTUM 3

void rr_tick(pcb_t* c) {
    if (--c->quantum_left == 0) {
        c->quantum_left = RR_QUANTUM;
        rr_rotate_to_tail(c);
        need_resched = 1;
    }
}
```

Three ticks is short enough that the shell stays responsive while a CPU-bound test runs, and long enough that we don't spend all our time in `context_switch`. We measured both extremes before picking it.

## Priority

Each PCB has a `priority` field, 0 (highest) to 7. `next()` walks the ready list and returns the lowest-numbered priority. Ties break by FCFS within the same priority. There is no aging in this policy, so a busy high-priority task will starve lower ones. That's a feature for now: the tests rely on it being deterministic.

```c
pcb_t* prio_next(void) {
    pcb_t* best = NULL;
    for (pcb_t* p = ready_head; p; p = p->next) {
        if (!best || p->priority < best->priority) best = p;
    }
    return best;
}
```

## Multi-Level Queue

Three bands: real-time (0), interactive (1), batch (2). Each band is its own FIFO. The scheduler always picks from the lowest non-empty band. Within a band, it acts like Round Robin with band-specific quanta: 1, 3, 8.

```c
#define MLQ_BANDS 3
static pcb_t* mlq_head[MLQ_BANDS];
static const uint32_t mlq_quantum[MLQ_BANDS] = {1, 3, 8};

pcb_t* mlq_next(void) {
    for (int b = 0; b < MLQ_BANDS; b++)
        if (mlq_head[b]) return mlq_head[b];
    return idle_task;
}
```

This gives us a cheap approximation of UNIX-style priority classes without the bookkeeping of dynamic priority adjustment. A process picks its band at creation and stays there.

## context_switch

The actual switch is a small assembly routine. We push six callee-saved registers, swap stacks, pop them on the other side, and return. The return address is whatever was on top of the new stack, which is either a `ret` into kernel code or an `iretq` frame into user mode.

```nasm
context_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov  [rdi], rsp        ; save old rsp
    mov  rsp, rsi          ; load new rsp
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbp
    pop  rbx
    ret
```

Six registers because that's the System V callee-saved set on x86_64. Caller-saved registers are already on the stack from whatever called `schedule()`, so we don't touch them.

## Timer Tick

IRQ 0 fires at 100 Hz from the PIT. The handler is short on purpose: bump a counter, call `tick()` on the policy, send EOI. The actual `schedule()` call happens on the path back to user mode.

```c
void timer_isr(void) {
    timer_ticks++;
    if (current) current_sched->tick(current);
    pic_eoi(0);
}
```

100 Hz is a good middle ground. Faster and we waste cycles on interrupt overhead; slower and `RR_QUANTUM=3` would feel sluggish.

## Stats

`get_stats` returns counters that the `top` command and the benchmarks both read. We keep them as raw 64-bit counters and never reset them mid-run.

```c
typedef struct {
    uint64_t switches;
    uint64_t ticks_idle;
    uint64_t ticks_user;
    uint64_t ready_count;
} sched_stats_t;
```
