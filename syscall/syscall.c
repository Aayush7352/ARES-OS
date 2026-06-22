//==============================================================================
// ARES OS - System call dispatcher (Phase 7)
//==============================================================================
// Routes INT 0x80 traps into kernel services. Until ring 3 lands, callers
// are kernel code, but the IDT entry is published with DPL=3 so the same
// vector keeps working once user-mode processes appear.
//
// Calling convention (rax = syscall #, rdi/rsi/rdx = args, rax = return).
// See syscall.h for the full table of SYS_* numbers and arg shapes.
//==============================================================================

#include <stddef.h>
#include <stdint.h>

#include "syscall.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "aresfs.h"
#include "irq.h"

//==============================================================================
// File descriptor table — wraps aresfs descriptors with kernel-side metadata.
//==============================================================================

static syscall_fd_t fd_table[SYSCALL_MAX_FDS];

static syscall_fd_t *fd_alloc(void)
{
    for (int i = 0; i < SYSCALL_MAX_FDS; i++) {
        if ((fd_table[i].flags & FD_FLAG_USED) == 0) {
            fd_table[i].fd        = i;
            fd_table[i].flags     = FD_FLAG_USED;
            fd_table[i].aresfs_fd = -1;
            fd_table[i].pos       = 0;
            return &fd_table[i];
        }
    }
    return NULL;
}

static void fd_release(syscall_fd_t *slot)
{
    if (slot == NULL) return;
    slot->flags     = 0;
    slot->aresfs_fd = -1;
    slot->pos       = 0;
}

syscall_fd_t *syscall_fd_from_fd(int fd)
{
    if (fd < 0 || fd >= SYSCALL_MAX_FDS) return NULL;
    if ((fd_table[fd].flags & FD_FLAG_USED) == 0) return NULL;
    return &fd_table[fd];
}

//==============================================================================
// Argument helpers — kernel and "user" share an address space for now, so a
// user pointer is just a uint64_t reinterpreted as a pointer.
//==============================================================================

static const char *path_from_user(uint64_t addr) { return (const char *)addr; }
static uint64_t    ret_from_int(int v)           { return (uint64_t)(int64_t)v; }

//==============================================================================
// Per-syscall handlers
//==============================================================================

static void sys_getpid(interrupt_frame_t *frame)
{
    frame->rax = (current_process != NULL)
                 ? (uint64_t)current_process->pid
                 : ret_from_int(-1);
}

static void sys_exit(interrupt_frame_t *frame)
{
    process_exit((int)(int32_t)frame->rdi); /* never returns */
    frame->rax = 0;                          /* defensive — unreachable */
}

static void sys_write(interrupt_frame_t *frame)
{
    uint64_t    fd    = frame->rdi;
    const char *buf   = (const char *)frame->rsi;
    size_t      count = (size_t)frame->rdx;

    if (fd == SYSCALL_FD_STDOUT || fd == SYSCALL_FD_STDERR) {
        console_puts(buf);
        frame->rax = (uint64_t)count;
        return;
    }

    syscall_fd_t *slot = syscall_fd_from_fd((int)fd);
    if (slot == NULL) { frame->rax = ret_from_int(-1); return; }

    int n = aresfs_write(slot->aresfs_fd, buf, count);
    if (n > 0) slot->pos += (uint32_t)n;
    frame->rax = ret_from_int(n);
}

static void sys_open(interrupt_frame_t *frame)
{
    const char *path  = path_from_user(frame->rdi);
    int         flags = (int)(int32_t)frame->rsi;

    int aresfs_fd = aresfs_open(path, flags);
    if (aresfs_fd < 0) { frame->rax = ret_from_int(-1); return; }

    syscall_fd_t *slot = fd_alloc();
    if (slot == NULL) {
        (void)aresfs_close(aresfs_fd);
        frame->rax = ret_from_int(-1);
        return;
    }
    slot->aresfs_fd = aresfs_fd;
    frame->rax      = (uint64_t)(int64_t)slot->fd;
}

static void sys_close(interrupt_frame_t *frame)
{
    syscall_fd_t *slot = syscall_fd_from_fd((int)(int32_t)frame->rdi);
    if (slot == NULL) { frame->rax = ret_from_int(-1); return; }
    (void)aresfs_close(slot->aresfs_fd);
    fd_release(slot);
    frame->rax = 0;
}

static void sys_read(interrupt_frame_t *frame)
{
    syscall_fd_t *slot = syscall_fd_from_fd((int)(int32_t)frame->rdi);
    if (slot == NULL) { frame->rax = ret_from_int(-1); return; }

    int n = aresfs_read(slot->aresfs_fd, (void *)frame->rsi,
                        (size_t)frame->rdx);
    if (n > 0) slot->pos += (uint32_t)n;
    frame->rax = ret_from_int(n);
}

static void sys_sleep(interrupt_frame_t *frame)
{
    uint64_t target = timer_ticks + frame->rdi;
    /* INT 0x80 is an interrupt gate, so IF is cleared on entry. Re-enable
       briefly so the timer IRQ can advance timer_ticks, then restore. */
    __asm__ volatile("sti");
    while (timer_ticks < target) __asm__ volatile("hlt");
    __asm__ volatile("cli");
    frame->rax = 0;
}

static void sys_ps(interrupt_frame_t *frame)
{
    process_ps();
    frame->rax = 0;
}

static void sys_gettime(interrupt_frame_t *frame)
{
    frame->rax = timer_ticks;
}

static void sys_mkdir(interrupt_frame_t *frame)
{
    fs_status_t st = aresfs_mkdir(path_from_user(frame->rdi));
    frame->rax     = ret_from_int((int)st);
}

static void sys_listdir(interrupt_frame_t *frame)
{
    fs_status_t st = aresfs_listdir(path_from_user(frame->rdi),
                                    (char *)frame->rsi,
                                    (size_t)frame->rdx);
    frame->rax     = ret_from_int((int)st);
}

static void sys_stat(interrupt_frame_t *frame)
{
    fs_status_t st = aresfs_stat(path_from_user(frame->rdi),
                                 (uint64_t *)frame->rsi,
                                 (uint32_t *)frame->rdx);
    frame->rax     = ret_from_int((int)st);
}

//==============================================================================
// Dispatcher
//==============================================================================

void syscall_handler(interrupt_frame_t *frame)
{
    switch (frame->rax) {
        case SYS_getpid:  sys_getpid(frame);  break;
        case SYS_exit:    sys_exit(frame);    break;
        case SYS_write:   sys_write(frame);   break;
        case SYS_open:    sys_open(frame);    break;
        case SYS_close:   sys_close(frame);   break;
        case SYS_read:    sys_read(frame);    break;
        case SYS_sleep:   sys_sleep(frame);   break;
        case SYS_ps:      sys_ps(frame);      break;
        case SYS_gettime: sys_gettime(frame); break;
        case SYS_mkdir:   sys_mkdir(frame);   break;
        case SYS_listdir: sys_listdir(frame); break;
        case SYS_stat:    sys_stat(frame);    break;
        default:          frame->rax = ret_from_int(-1); break;
    }
}

//==============================================================================
// Initialization
//==============================================================================

void syscall_init(void)
{
    for (int i = 0; i < SYSCALL_MAX_FDS; i++) {
        fd_table[i].fd        = i;
        fd_table[i].flags     = 0;
        fd_table[i].aresfs_fd = -1;
        fd_table[i].pos       = 0;
    }

    interrupt_register_handler(0x80, syscall_handler);
    /* Lift the IDT entry to DPL=3 so user mode (when added) can `int 0x80`. */
    interrupt_set_dpl(0x80, IDT_FLAG_DPL_3);

    console_writeline("[syscall] Syscall interface initialized (INT 0x80)");
}
