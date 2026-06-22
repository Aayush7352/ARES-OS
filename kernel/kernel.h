#ifndef ARES_KERNEL_H
#define ARES_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Kernel version */
#define KERNEL_NAME         "ARES OS"
#define KERNEL_VERSION      "0.1.0"
#define KERNEL_ARCH         "x86_64"

/* Kernel constants */
#define KERNEL_BASE_ADDR    0x100000
#define KERNEL_STACK_SIZE   0x4000   /* 16KB initial stack */

/* Boot info passed from bootloader */
typedef struct {
    uint32_t magic;             /* Magic number for verification */
    uint32_t boot_drive;        /* BIOS boot drive number */
    uint64_t memory_size;       /* Total memory in bytes */
    uint64_t kernel_phys_addr;  /* Physical load address */
} boot_info_t;

/* System status codes */
typedef enum {
    ARES_OK        = 0,
    ARES_ERROR     = -1,
    ARES_ENOMEM    = -2,
    ARES_EINVAL    = -3,
    ARES_ENODEV    = -4,
    ARES_EBUSY     = -5,
} ares_status_t;

/* Architecture-specific initialization */
void arch_early_init(void);
void arch_init(void);

/* Kernel subsystems */
void kernel_early_init(void);
void kernel_main(void) __attribute__((noreturn));

#endif /* ARES_KERNEL_H */
