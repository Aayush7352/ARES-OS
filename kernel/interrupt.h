#ifndef ARES_INTERRUPT_H
#define ARES_INTERRUPT_H

#include <stdint.h>

/* IDT entry - 16 bytes each in long mode */
typedef struct {
    uint16_t offset_low;    /* bits 0-15 of handler address */
    uint16_t selector;      /* code segment selector (0x08 for kernel) */
    uint8_t  ist;           /* Interrupt Stack Table offset (0-7) */
    uint8_t  flags;         /* type, DPL, present bit */
    uint16_t offset_mid;    /* bits 16-31 of handler address */
    uint32_t offset_high;   /* bits 32-63 of handler address */
    uint32_t reserved;      /* must be zero */
} __attribute__((packed)) idt_entry_t;

/* IDTR structure for LIDT instruction */
typedef struct {
    uint16_t limit;         /* size of IDT - 1 */
    uint64_t base;          /* linear address of IDT */
} __attribute__((packed)) idtr_t;

/* Register state saved by interrupt handler on stack */
typedef struct {
    /* Saved by isr_common (15 registers) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* Pushed by ISR stub */
    uint64_t int_no;
    uint64_t err_code;
    /* Pushed by CPU (ring 0: 3 entries; ring 3→0: 5 entries) */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;           /* only present if CPL changed */
    uint64_t ss;            /* only present if CPL changed */
} __attribute__((packed)) interrupt_frame_t;

/* Interrupt handler function type */
typedef void (*isr_handler_t)(interrupt_frame_t *frame);

/* IDT flag constants */
#define IDT_FLAG_PRESENT     0x80
#define IDT_FLAG_DPL_0       0x00
#define IDT_FLAG_DPL_3       0x60
#define IDT_FLAG_INTERRUPT   0x0E   /* 32/64-bit interrupt gate */
#define IDT_FLAG_TRAP        0x0F   /* 32/64-bit trap gate */

/* CPU exception vector numbers */
#define INT_DIVIDE_ZERO      0
#define INT_DEBUG            1
#define INT_NMI              2
#define INT_BREAKPOINT       3
#define INT_OVERFLOW         4
#define INT_BOUND_RANGE      5
#define INT_INVALID_OPCODE   6
#define INT_DEVICE_NA        7
#define INT_DOUBLE_FAULT     8
#define INT_COPROC_SEG       9
#define INT_INVALID_TSS      10
#define INT_SEG_NOT_PRESENT  11
#define INT_STACK_FAULT      12
#define INT_GP_FAULT         13
#define INT_PAGE_FAULT       14
#define INT_RESERVED_15      15
#define INT_FPU_ERROR        16
#define INT_ALIGN_CHECK      17
#define INT_MACHINE_CHECK    18
#define INT_SIMD_ERROR       19
#define INT_VIRT_ERROR       20

#define INT_VECTORS          256

/* ISR stub table (defined in assembly) */
extern uint64_t isr_stub_table[INT_VECTORS];

/* Initialize the IDT and load it */
void interrupt_init(void);

/* Register a handler for a specific interrupt vector */
void interrupt_register_handler(uint8_t vector, isr_handler_t handler);

/* Update DPL of an IDT entry (for syscall vector) */
void interrupt_set_dpl(uint8_t vector, uint8_t dpl);

/* Get human-readable name for a CPU exception */
const char *interrupt_get_exception_name(uint8_t vector);

/* C-level ISR handler called from assembly (must be visible to asm) */
void isr_handler(interrupt_frame_t *frame);

#endif /* ARES_INTERRUPT_H */