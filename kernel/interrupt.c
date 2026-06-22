//==============================================================================
// ARES OS - Interrupt Descriptor Table and Exception Handling
//==============================================================================
// Manages the Interrupt Descriptor Table (IDT) for x86_64 long mode.
// Provides default handlers for all 32 CPU exceptions and a registration
// mechanism for custom interrupt handlers (IRQs, system calls, etc.).
//==============================================================================

#include "interrupt.h"
#include "console.h"
#include "kernel.h"
#include "../lib/io.h"

//==============================================================================
// Static Data
//==============================================================================

#define IDT_ENTRIES 256

/* Aligned IDT — lidt requires the table to be naturally aligned */
/* VOLATILE: prevents GCC -O2 from eliminating stores as "dead"
   since the CPU reads IDT via LIDT, not through C code. */
static volatile idt_entry_t idt[IDT_ENTRIES] __attribute__((aligned(16)));

/* Registered handler table — NULL means default handling */
static isr_handler_t isr_handlers[IDT_ENTRIES];

/* Human-readable names for CPU exception vectors 0-31 */
static const char *const exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Floating-Point Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
};

//==============================================================================
// IDT Entry Configuration
//==============================================================================

static void idt_set_descriptor(uint8_t vector, uint64_t handler_addr, uint8_t flags)
{
    volatile idt_entry_t *entry = &idt[vector];
    entry->offset_low  = (uint16_t)(handler_addr & 0xFFFF);
    entry->offset_mid  = (uint16_t)((handler_addr >> 16) & 0xFFFF);
    entry->offset_high = (uint32_t)((handler_addr >> 32) & 0xFFFFFFFF);
    entry->selector    = 0x08;   /* kernel code segment */
    entry->ist         = 0;      /* no IST switching */
    entry->flags       = flags;
    entry->reserved    = 0;
}

//==============================================================================
// Default Exception Handler (prints diagnostic and halts)
//==============================================================================

static void exception_default_handler(interrupt_frame_t *frame)
{
    uint8_t vector = (uint8_t)frame->int_no;

    console_set_color(VGA_LIGHT_RED, VGA_BLACK);
    console_printf("\n!!! KERNEL PANIC: %s (%d) !!!\n",
                   interrupt_get_exception_name(vector), vector);
    console_printf("    Error code: 0x%x\n", frame->err_code);
    console_printf("    RIP: 0x%p    CS: 0x%x\n",
                   (void *)frame->rip, frame->cs);
    console_printf("    RFLAGS: 0x%x    RSP: 0x%p\n",
                   frame->rflags, (void *)frame->rsp);
    console_printf("    RAX: 0x%p    RBX: 0x%p    RCX: 0x%p\n",
                   (void *)frame->rax, (void *)frame->rbx,
                   (void *)frame->rcx);
    console_printf("    RDX: 0x%p    RSI: 0x%p    RDI: 0x%p\n",
                   (void *)frame->rdx, (void *)frame->rsi,
                   (void *)frame->rdi);
    console_printf("    RBP: 0x%p    R8:  0x%p    R9:  0x%p\n",
                   (void *)frame->rbp, (void *)frame->r8,
                   (void *)frame->r9);
    console_printf("    R10: 0x%p    R11: 0x%p    R12: 0x%p\n",
                   (void *)frame->r10, (void *)frame->r11,
                   (void *)frame->r12);
    console_printf("    R13: 0x%p    R14: 0x%p    R15: 0x%p\n",
                   (void *)frame->r13, (void *)frame->r14,
                   (void *)frame->r15);

    /* Page Fault diagnostics */
    if (vector == INT_PAGE_FAULT) {
        uint64_t cr2_value;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2_value));
        console_printf("    CR2 (fault address): 0x%p\n",
                       (void *)cr2_value);
        console_printf("    Page fault %s: %s\n",
                       (frame->err_code & 0x01) ? "(present)" : "(non-present)",
                       (frame->err_code & 0x02) ? "write" : "read");
        if (frame->err_code & 0x04) {
            console_printf("    Fault in user mode (CPL=3)\n");
        }
        if (frame->err_code & 0x08) {
            console_printf("    Reserved bit violation\n");
        }
        if (frame->err_code & 0x10) {
            console_printf("    Instruction fetch\n");
        }
    }

    console_printf("!!! System halted !!!\n");

    /* Disable interrupts and halt forever */
    __asm__ volatile("cli; hlt");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

//==============================================================================
// C-level ISR Handler (called from interrupt.asm isr_common)
//==============================================================================

void isr_handler(interrupt_frame_t *frame)
{
    uint8_t vector = (uint8_t)frame->int_no;

    /* Call registered handler if present */
    if (isr_handlers[vector] != NULL) {
        isr_handlers[vector](frame);
        /* Handler already processed; EOI not needed here —
           the handler may have sent it, or it was a CPU exception
           that wouldn't need one.  IRQ handlers registered via
           interrupt_register_handler are responsible for EOI. */
        return;
    }

    /* Default handling for CPU exceptions (vectors 0-31) */
    if (vector < 32) {
        exception_default_handler(frame);
        return; /* never reached */
    }

    /* Default handling for IRQ vectors — just acknowledge the PIC */
    if (vector >= 0x20 && vector <= 0x2F) {
        if (vector >= 0x28) {
            outb(0xA0, 0x20);   /* slave PIC EOI */
        }
        outb(0x20, 0x20);       /* master PIC EOI */
    }

    /* Other unhandled vectors — silently ignored */
}

//==============================================================================
// Public API
//==============================================================================

void interrupt_init(void)
{
    /* Populate all 256 IDT entries from the assembly stub table */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_descriptor((uint8_t)i, isr_stub_table[i],
                           IDT_FLAG_PRESENT | IDT_FLAG_INTERRUPT);
    }

    /* Build the IDTR and load it */
    idtr_t idtr;
    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint64_t)&idt;

    __asm__ volatile("lidt %0" : : "m"(idtr));

    console_writeline("[int] IDT loaded — 256 entries configured");
}

void interrupt_register_handler(uint8_t vector, isr_handler_t handler)
{
    isr_handlers[vector] = handler;
}

void interrupt_set_dpl(uint8_t vector, uint8_t dpl)
{
    idt[vector].flags = (uint8_t)(IDT_FLAG_PRESENT | dpl | IDT_FLAG_INTERRUPT);
}

const char *interrupt_get_exception_name(uint8_t vector)
{
    if (vector < sizeof(exception_names) / sizeof(exception_names[0])) {
        return exception_names[vector];
    }
    return "Unknown Exception";
}

