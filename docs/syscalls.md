# ARES OS, System Calls

System calls are how userspace asks the kernel to do something. ARES uses the classic `INT 0x80` trap rather than `syscall`/`sysret`, because the legacy interrupt path is easier to debug and we don't yet have separate user/kernel address spaces that would benefit from the faster instruction.

## Calling Convention

The convention borrows from Linux's old i386 ABI and lifts it to 64-bit registers. The syscall number lives in `rax`. Arguments go in `rdi`, `rsi`, `rdx`, `r10`, `r8`, in that order. The return value comes back in `rax`. Negative values are errors (matching `ares_status_t`).

```text
rax = syscall number
rdi = arg 0
rsi = arg 1
rdx = arg 2
r10 = arg 3    (not rcx, which int 0x80 clobbers)
r8  = arg 4

int 0x80

rax = return value or -errno
```

The IDT entry for vector `0x80` is a trap gate, not an interrupt gate, so `IF` stays as it was. We don't want syscalls to silently mask interrupts.

## Dispatcher

The handler saves the trap frame, looks up the syscall by number, calls it with the argument registers cast to a uniform prototype, and returns the result.

```c
typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT]     = sys_exit,
    [SYS_WRITE]    = sys_write,
    [SYS_READ]     = sys_read,
    [SYS_OPEN]     = sys_open,
    [SYS_CLOSE]    = sys_close,
    [SYS_SEEK]     = sys_seek,
    [SYS_MKDIR]    = sys_mkdir,
    [SYS_UNLINK]   = sys_unlink,
    [SYS_LISTDIR]  = sys_listdir,
    [SYS_STAT]     = sys_stat,
    [SYS_GETPID]   = sys_getpid,
    [SYS_YIELD]    = sys_yield,
};

int64_t syscall_dispatch(uint64_t n, uint64_t a0, uint64_t a1,
                         uint64_t a2, uint64_t a3, uint64_t a4) {
    if (n >= SYSCALL_COUNT || !syscall_table[n]) return -ARES_EINVAL;
    return syscall_table[n](a0, a1, a2, a3, a4);
}
```

Out-of-range numbers fail with `EINVAL` instead of crashing. The table is sparse-friendly because gaps stay NULL and the bounds check catches them.

## The Twelve Calls

| Num | Name      | Signature                                          | Returns        |
|----:|-----------|----------------------------------------------------|----------------|
|  0  | exit      | `void exit(int code)`                              | does not return|
|  1  | write     | `int write(int fd, const void* buf, size_t n)`     | bytes written  |
|  2  | read      | `int read(int fd, void* buf, size_t n)`            | bytes read     |
|  3  | open      | `int open(const char* path, int flags)`            | fd or -err     |
|  4  | close     | `int close(int fd)`                                | 0 or -err      |
|  5  | seek      | `int seek(int fd, int off, int whence)`            | new offset     |
|  6  | mkdir     | `int mkdir(const char* path)`                      | 0 or -err      |
|  7  | unlink    | `int unlink(const char* path)`                     | 0 or -err      |
|  8  | listdir   | `int listdir(const char* path, dirent_t* out, int)`| count          |
|  9  | stat      | `int stat(const char* path, stat_t* out)`          | 0 or -err      |
| 10  | getpid    | `int getpid(void)`                                 | pid            |
| 11  | yield     | `int yield(void)`                                  | 0              |

Twelve is the minimum that lets the shell and editor work. We resisted the urge to add `fork`, `exec`, `pipe`, and friends, because each of them implies a chunk of design work that hasn't been done yet.

## File Descriptors

Each PCB owns an `fd_table[8]`. Indices 0, 1, 2 are reserved for stdin, stdout, stderr at process creation; they point to the console. Indices 3..7 are free.

```c
typedef struct {
    uint32_t inum;        /* 0 = free, special = console */
    uint32_t offset;
    uint32_t flags;
} fd_entry_t;
```

`sys_open` finds a free slot, fills it in, returns the index. `sys_close` clears the slot. Reading from a closed FD returns `-ARES_EINVAL`. Reading past EOF returns 0, matching POSIX.

The 8-slot cap is intentional. It's enough to compile, run, and read a file at once. Growing it later is mechanical.

## Error Handling

Errors come back as the negative of an `ares_status_t`. The user-side wrapper turns negative returns into a positive errno-like field set on the caller's `errno` global. Inside the kernel we never raise exceptions; every failure walks back up the stack through return values.

```c
/* userspace wrapper */
int write(int fd, const void* buf, size_t n) {
    int64_t r = syscall3(SYS_WRITE, fd, (uint64_t)buf, n);
    if (r < 0) { errno = -r; return -1; }
    return (int)r;
}
```

The negative-return convention means a syscall can never legally return `-1` to `-6` as a success value. That's fine for the calls we have, and it's what every well-behaved UNIX has done forever.

## Pointer Validation

Right now there's no user/kernel address split, so a "bad pointer" from a syscall is just a pointer outside the PMM-mapped range. `sys_write` and friends call `vmm_translate` on the buffer's start and end and fail with `EINVAL` if either doesn't map. It's not a security check (there's no untrusted code yet), it's a crash-prevention check.

```c
static int validate_user_ptr(const void* p, size_t n) {
    uint64_t a = (uint64_t)p;
    if (vmm_translate(a) == 0)         return -ARES_EINVAL;
    if (vmm_translate(a + n - 1) == 0) return -ARES_EINVAL;
    return 0;
}
```

When we add user-mode processes, this turns into a real permission check that also enforces `PTE_USER`.

## Why INT 0x80

`syscall`/`sysret` is faster and what real x86_64 OSes use. We picked `INT 0x80` because the trap path goes through the same IDT and exception-frame layout as everything else in the kernel, so the debugging tooling already works. Once we have a stable user-mode story, switching to `syscall` is a localized change in the dispatcher and the userspace wrapper.
