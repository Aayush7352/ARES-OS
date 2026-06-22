//==============================================================================
// ARES OS - Programmable Interrupt Controller (PIC) Management
//==============================================================================
// Handles the 8259A PIC initialization, remapping IRQ vectors from the BIOS
// defaults (0x08-0x0F, which conflict with CPU exceptions) to 0x20-0x2F.
// Registers default handlers for the PIT timer (IRQ0) and PS/2 keyboard (IRQ1).
//==============================================================================

#include "irq.h"
#include "interrupt.h"
#include "scheduler.h"
#include "console.h"
#include "kernel.h"
#include "../lib/io.h"
#include "keyboard.h"

//==============================================================================
// Global Tick Counter
//==============================================================================

volatile uint64_t timer_ticks = 0;

//==============================================================================
// Timer IRQ Handler (IRQ0 → INT 0x20)
//==============================================================================

static void timer_handler(interrupt_frame_t *frame)
{
    (void)frame;
    timer_ticks++;
    /* Notify scheduler of timer tick (for preemptive algorithms) */
    scheduler_tick();
    /* Send EOI */
    outb(0x20, 0x20);
}

//==============================================================================
// Keyboard IRQ Handler (IRQ1 → INT 0x21)
//==============================================================================

static void keyboard_handler(interrupt_frame_t *frame)
{
    (void)frame;
    keyboard_irq_handler();
}

//==============================================================================
// PIC Initialization
//==============================================================================
//
// The 8259A PIC is initialized with four Initialization Command Words:
//
//   ICW1 — start init sequence (0x11 = edge triggered, cascade, ICW4)
//   ICW2 — vector offset for IRQ0 (master = 0x20, slave = 0x28)
//   ICW3 — cascade wiring (master = 0x04 [IRQ2 has slave], slave = 0x02)
//   ICW4 — x86 mode, normal EOI, non-buffered (0x01)
//
//==============================================================================

void irq_init(void)
{
    /* Save original masks (will restore modified version) */
    /* uint8_t mask_master = inb(PIC_MASTER_DATA); */
    /* uint8_t mask_slave  = inb(PIC_SLAVE_DATA); */

    /* ICW1: begin initialization */
    outb(PIC_MASTER_CMD, 0x11);
    outb(PIC_SLAVE_CMD,  0x11);

    /* ICW2: set vector offsets */
    outb(PIC_MASTER_DATA, 0x20);   /* master: 0x20-0x27 */
    outb(PIC_SLAVE_DATA,  0x28);   /* slave:  0x28-0x2F */

    /* ICW3: tell master there is a slave at IRQ2 (0x04);
             tell slave its cascade identity (0x02) */
    outb(PIC_MASTER_DATA, 0x04);
    outb(PIC_SLAVE_DATA,  0x02);

    /* ICW4: x86 mode, normal EOI */
    outb(PIC_MASTER_DATA, 0x01);
    outb(PIC_SLAVE_DATA,  0x01);

    /* Unmask all IRQs (0 = unmasked) */
    outb(PIC_MASTER_DATA, 0x00);
    outb(PIC_SLAVE_DATA,  0x00);

    /* Register handlers */
    interrupt_register_handler(IRQ0_TIMER,    timer_handler);
    interrupt_register_handler(IRQ1_KEYBOARD, keyboard_handler);

    console_writeline("[irq] PIC remapped — IRQ handlers installed");
}