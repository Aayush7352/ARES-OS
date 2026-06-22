# ARES OS, Security

ARES has a small but real security model. There are user accounts, a login flow, hashed passwords, and per-file permission bits. Everything is enforced at the syscall boundary inside the kernel. None of this is production-grade cryptography. It exists to teach the shape of UNIX-style access control and to give the shell something to talk to.

## User Accounts

The user table is fixed-size, eight slots, sitting in the kernel BSS and persisted to a reserved block on disk. Slot 0 is always root.

```c
#define USER_MAX        8
#define USERNAME_MAX    16
#define PASSWORD_HASH_LEN 16

typedef struct {
    uint32_t uid;
    uint32_t gid;
    char     name[USERNAME_MAX];
    uint8_t  pw_hash[PASSWORD_HASH_LEN];
    uint8_t  pw_salt[8];
    uint32_t flags;             /* USER_ACTIVE, USER_LOCKED */
} user_account_t;

extern user_account_t user_table[USER_MAX];
```

`uid 0` is root. Everyone else gets uid 1 through 7. The group field is currently a copy of the uid, because we don't have group membership lists yet. The shape is here so adding groups later is a localized change.

## Password Hashing

The "hash" is a salted XOR-fold. It's deliberately simple, and it's the one piece of this kernel I would never recommend reading as a guide to real cryptography.

```c
void password_hash(const char* pw, const uint8_t salt[8],
                   uint8_t out[PASSWORD_HASH_LEN])
{
    uint8_t state[PASSWORD_HASH_LEN] = {0};
    for (int i = 0; i < 8; i++) state[i] = salt[i];

    for (size_t i = 0; pw[i]; i++) {
        state[i % PASSWORD_HASH_LEN] ^= (uint8_t)pw[i];
        state[(i + 7) % PASSWORD_HASH_LEN] += (uint8_t)pw[i] * 131;
    }
    for (int round = 0; round < 64; round++)
        for (int i = 0; i < PASSWORD_HASH_LEN; i++)
            state[i] = state[i] ^ state[(i + 5) % PASSWORD_HASH_LEN] ^ round;

    for (int i = 0; i < PASSWORD_HASH_LEN; i++) out[i] = state[i];
}
```

It's not collision-resistant and it's not slow. A real OS would use something like Argon2 or scrypt. We picked XOR-fold because the kernel has no crypto library, and writing one would be a project of its own. The interface is what matters: salt in, hash out, comparison by `memcmp`. Swapping in a real KDF later is a one-function change.

## Login Flow

`login` runs before the shell starts. It loops until a valid username and password are accepted, then sets the active uid in the current process.

```c
ares_status_t login_prompt(void) {
    char name[USERNAME_MAX], pw[64];
    for (;;) {
        printf("login: ");
        shell_read_line(name, sizeof(name));
        printf("password: ");
        shell_read_line_silent(pw, sizeof(pw));

        user_account_t* u = user_lookup(name);
        if (u && user_check_password(u, pw) == ARES_OK) {
            current_uid = u->uid;
            current_gid = u->gid;
            printf("welcome, %s\n", u->name);
            return ARES_OK;
        }
        printf("login incorrect\n");
        delay_ms(500);
    }
}
```

The half-second delay after a failure is the only rate-limiting we do. It's enough to make a typo annoying and an automated guess pointless on a single-process kernel. `shell_read_line_silent` echoes nothing to the screen, so shoulder surfing fails too.

## Permission Bits

Each inode's `mode` field holds nine bits, the familiar `rwxrwxrwx` layout, plus the inode's `uid` and `gid`.

```c
#define PERM_OWNER_R  0400
#define PERM_OWNER_W  0200
#define PERM_OWNER_X  0100
#define PERM_GROUP_R  0040
#define PERM_GROUP_W  0020
#define PERM_GROUP_X  0010
#define PERM_OTHER_R  0004
#define PERM_OTHER_W  0002
#define PERM_OTHER_X  0001
```

The octal constants are intentional. They match UNIX habit, and `chmod 644` is going to feel right to anyone who's used a real shell.

## auth_check

Every FS operation that touches an inode goes through `auth_check` before it does anything else. This is the single chokepoint.

```c
typedef enum { AUTH_READ, AUTH_WRITE, AUTH_EXEC } auth_op_t;

ares_status_t auth_check(const ares_inode_t* ino, auth_op_t op) {
    if (current_uid == 0) return ARES_OK;             /* root bypass */

    uint32_t bits;
    if (current_uid == ino->uid)      bits = (ino->mode >> 6) & 7;
    else if (current_gid == ino->gid) bits = (ino->mode >> 3) & 7;
    else                              bits = ino->mode & 7;

    uint32_t need = (op == AUTH_READ)  ? 4 :
                    (op == AUTH_WRITE) ? 2 : 1;
    return (bits & need) ? ARES_OK : ARES_EPERM;
}
```

Root bypasses the check. Everyone else falls into one of three classes: owner, group, other. The matching `rwx` triplet decides. This is exactly the POSIX rule, with no ACLs and no capabilities.

The call sites are in `fs_open`, `fs_unlink`, `fs_mkdir`, and `fs_write`. Reads check on `open`, writes check on every `write` because the FD's flags can be changed by `seek` in ways the open didn't anticipate. That's slightly stricter than POSIX, and it's a deliberate choice.

## Privilege Boundary

There's no user-mode yet, so the privilege boundary is enforced by convention inside the kernel. `current_uid` is a per-process field; the shell sets it at login and never changes it without going through `login` again. A buggy command can in theory write `current_uid = 0` to itself, and the kernel won't notice. That's a known limitation, and it's the main thing that changes when we add real user-mode pages.

## passwd

The shell's `passwd` command changes the current user's password. It asks for the old one, asks for the new one twice, hashes the new one with a fresh random salt, and writes the user table back to disk.

```c
int cmd_passwd(int argc, char** argv) {
    user_account_t* u = user_lookup_by_uid(current_uid);
    if (!u) return ARES_EPERM;

    char old_pw[64], new_pw[64], confirm[64];
    /* prompt for old_pw, verify */
    /* prompt for new_pw and confirm, compare */

    rng_bytes(u->pw_salt, sizeof(u->pw_salt));
    password_hash(new_pw, u->pw_salt, u->pw_hash);
    return user_table_save();
}
```

`rng_bytes` mixes the RTC, the kernel tick counter, and some keystroke timing into a tiny xorshift state. It's not cryptographically strong. It's strong enough that two consecutive `passwd` calls don't share a salt.

## What's Missing

There's no setuid, no capabilities, no audit log beyond the kernel ring buffer, no process isolation, and no way to drop privileges mid-session. None of that is hard to add once the kernel has real user-mode. The current model is honest about being a teaching surface: the shape is right, the strength is not.
