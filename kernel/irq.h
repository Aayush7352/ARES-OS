#ifndef ARES_IRQ_H
#define ARES_IRQ_H

#include <stdint.h>

/* PIC I/O ports */
#define PIC_MASTER_CMD   0x20
#define PIC_MASTER_DATA  0x21
#define PIC_SLAVE_CMD    0xA0
#define PIC_SLAVE_DATA   0xA1
#define PIC_EOI          0x20

/* IRQ vector numbers (after PIC remap to INT 0x20-0x2F) */
#define IRQ0_TIMER       0x20
#define IRQ1_KEYBOARD    0x21
#define IRQ2_CASCADE     0x22
#define IRQ3_COM2        0x23
#define IRQ4_COM1        0x24
#define IRQ5_LPT2        0x25
#define IRQ6_FLOPPY      0x26
#define IRQ7_LPT1        0x27
#define IRQ8_RTC         0x28
#define IRQ9_ACPI        0x29
#define IRQ10_SCI        0x2A
#define IRQ11_USB        0x2B
#define IRQ12_MOUSE      0x2C
#define IRQ13_FPU        0x2D
#define IRQ14_ATA_PRIM   0x2E
#define IRQ15_ATA_SEC    0x2F

/* System tick counter (incremented by timer IRQ) */
extern volatile uint64_t timer_ticks;

/* Initialize the PIC and register default IRQ handlers */
void irq_init(void);

#endif /* ARES_IRQ_H */