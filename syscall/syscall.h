#ifndef ARES_SYSCALL_H
#define ARES_SYSCALL_H

#include <stdint.h>
#include "interrupt.h"

/*------------------------------------------------------------------------*/
/* ARES OS - System call interface                                        */
/*                                                                        */
/* Phase 7 syscall layer. INT 0x80 is the trap vector — no ring 3 yet,    */
/* so callers are kernel code running in CPL 0, but the IDT entry is set  */
/* to DPL=3 so the same vector will work once user mode arrives.          */
/*                                                                        */
/* Calling convention (mirrors System V layout for natural reuse):        */
/*   rax = syscall number                                                 */
/*   rdi = arg1                                                           */
/*   rsi = arg2                                                           */
/*   rdx = arg3                                                           */
/*   rax = return value on resume (negative = error)                      */
/*------------------------------------------------------------------------*/

/* Syscall numbers */
#define SYS_getpid          0
#define SYS_exit            1
#define SYS_write           2
#define SYS_open            3
#define SYS_close           4
#define SYS_read            5
#define SYS_sleep           6
#define SYS_ps              7
#define SYS_gettime         8
#define SYS_mkdir           9
#define SYS_listdir         10
#define SYS_stat            11
#define SYS_SYSCALL_COUNT   12

/* Reserved file descriptors (POSIX-style). */
#define SYSCALL_FD_STDIN    0
#define SYSCALL_FD_STDOUT   1
#define SYSCALL_FD_STDERR   2

/* File descriptor table */
#define SYSCALL_MAX_FDS     16

/* FD flags */
#define FD_FLAG_USED        0x01

typedef struct {
    int      fd;             /* file descriptor number               */
    uint8_t  flags;          /* FD_FLAG_* bits                       */
    int      aresfs_fd;      /* underlying aresfs file descriptor    */
    uint32_t pos;            /* current position in file             */
} syscall_fd_t;

/* Initialize syscall system - registers INT 0x80 handler and sets DPL=3 */
void syscall_init(void);

/* Syscall handler (registered for INT 0x80) */
void syscall_handler(interrupt_frame_t *frame);

/* FD table lookup. Returns NULL if `fd` is out of range or not in use. */
syscall_fd_t *syscall_fd_from_fd(int fd);

#endif /* ARES_SYSCALL_H */
