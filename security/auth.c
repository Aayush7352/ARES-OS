/*==========================================================================*/
/* ARES OS - User authentication                                              */
/*                                                                            */
/* In-memory user account table plus a tiny console login prompt. The hash    */
/* used here is a simple XOR fold; it is NOT cryptographically sound and is   */
/* only suitable for the educational shell login flow.                        */
/*==========================================================================*/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "auth.h"
#include "console.h"
#include "keyboard.h"

/*--------------------------------------------------------------------------*/
/* Globals (declared extern in auth.h).                                     */
/*--------------------------------------------------------------------------*/

user_account_t g_users[MAX_USERS];
int            g_user_count   = 0;
int            g_current_uid  = -1;     /* No user logged in at boot.       */

/*--------------------------------------------------------------------------*/
/* Tiny freestanding string / memory helpers.                               */
/*--------------------------------------------------------------------------*/

static void auth_memset(void *dst, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = val;
}

static size_t auth_strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int auth_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/* Copy up to (cap - 1) bytes from src to dst and NUL-terminate. */
static void auth_strcpy_n(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0U) return;
    while (i + 1U < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/*--------------------------------------------------------------------------*/
/* Internal: read a single line from the keyboard.                          */
/*                                                                          */
/* Echoes the typed character verbatim unless `mask` is true, in which case */
/* it echoes '*' (used for the password prompt). Supports backspace; all    */
/* other special keys are ignored. The caller-supplied buffer is always     */
/* NUL-terminated on return.                                                */
/*--------------------------------------------------------------------------*/

static void auth_read_line(char *buf, size_t cap, bool mask) {
    size_t pos = 0;
    if (cap == 0U) return;

    for (;;) {
        __asm__ volatile("hlt");

        while (keyboard_has_data()) {
            uint8_t c = keyboard_getchar();
            if (c == 0U) continue;

            if (c == KEY_ENTER) {
                console_putchar('\n');
                buf[pos] = '\0';
                return;
            } else if (c == KEY_BACKSPACE) {
                if (pos > 0U) {
                    pos--;
                    buf[pos] = '\0';
                    console_putchar('\b');
                    console_putchar(' ');
                    console_putchar('\b');
                }
            } else if (c >= 0x20 && c < 0x7F && pos + 1U < cap) {
                buf[pos] = (char)c;
                pos++;
                console_putchar(mask ? '*' : (char)c);
            }
            /* Other special keys (arrows, F-keys, etc.) ignored. */
        }
    }
}

/*--------------------------------------------------------------------------*/
/* Public API                                                               */
/*--------------------------------------------------------------------------*/

void auth_hash_password(const char *password, uint8_t *hash_out) {
    if (password == NULL || hash_out == NULL) return;

    for (size_t i = 0; i < (size_t)PASS_HASH_LEN; i++) hash_out[i] = 0U;

    for (size_t i = 0; password[i] != '\0'; i++) {
        size_t  idx = i % (size_t)PASS_HASH_LEN;
        uint8_t mix = (uint8_t)((uint8_t)password[i] + (uint8_t)i);
        hash_out[idx] = (uint8_t)(hash_out[idx] ^ mix);
    }
}

void auth_init(void) {
    auth_memset(g_users, 0, sizeof(g_users));
    g_user_count  = 0;
    g_current_uid = -1;

    /* Slot 0: root / root123 (uid=0, gid=0, superuser). */
    auth_strcpy_n(g_users[0].username, "root", (size_t)USERNAME_MAX);
    auth_hash_password("root123", g_users[0].password_hash);
    g_users[0].uid     = 0U;
    g_users[0].gid     = 0U;
    g_users[0].is_root = true;
    g_users[0].active  = true;

    /* Slot 1: admin / admin123 (uid=1, gid=1, superuser). */
    auth_strcpy_n(g_users[1].username, "admin", (size_t)USERNAME_MAX);
    auth_hash_password("admin123", g_users[1].password_hash);
    g_users[1].uid     = 1U;
    g_users[1].gid     = 1U;
    g_users[1].is_root = true;
    g_users[1].active  = true;

    /* Slot 2: user / user123 (uid=2, gid=100, regular). */
    auth_strcpy_n(g_users[2].username, "user", (size_t)USERNAME_MAX);
    auth_hash_password("user123", g_users[2].password_hash);
    g_users[2].uid     = 2U;
    g_users[2].gid     = 100U;
    g_users[2].is_root = false;
    g_users[2].active  = true;

    g_user_count = 3;
}

int auth_login(const char *username, const char *password) {
    if (username == NULL || password == NULL) return -1;

    uint8_t hash[PASS_HASH_LEN];
    auth_hash_password(password, hash);

    for (int i = 0; i < MAX_USERS; i++) {
        if (!g_users[i].active) continue;
        if (auth_strcmp(g_users[i].username, username) != 0) continue;

        bool match = true;
        for (size_t j = 0; j < (size_t)PASS_HASH_LEN; j++) {
            if (g_users[i].password_hash[j] != hash[j]) {
                match = false;
                break;
            }
        }
        if (match) return (int)g_users[i].uid;
    }
    return -1;
}

int auth_create_user(const char *username, const char *password, bool is_root) {
    if (username == NULL || password == NULL) return -1;
    if (username[0] == '\0') return -1;
    if (auth_strlen(username) >= (size_t)USERNAME_MAX) return -1;

    /* Reject duplicate usernames. */
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].active
            && auth_strcmp(g_users[i].username, username) == 0) {
            return -1;
        }
    }

    /* Pick the first free slot, and one-past the largest live uid. */
    int      slot     = -1;
    uint32_t next_uid = 0U;
    for (int i = 0; i < MAX_USERS; i++) {
        if (!g_users[i].active) {
            if (slot < 0) slot = i;
        } else if (g_users[i].uid >= next_uid) {
            next_uid = g_users[i].uid + 1U;
        }
    }
    if (slot < 0) return -1;

    user_account_t *u = &g_users[slot];
    auth_strcpy_n(u->username, username, (size_t)USERNAME_MAX);
    auth_hash_password(password, u->password_hash);
    u->uid     = next_uid;
    u->gid     = is_root ? next_uid : 100U;
    u->is_root = is_root;
    u->active  = true;

    g_user_count++;
    return (int)next_uid;
}

int auth_delete_user(uint32_t uid) {
    if (uid == 0U) return -1;   /* Refuse to delete root. */

    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].active && g_users[i].uid == uid) {
            auth_memset(&g_users[i], 0, sizeof(g_users[i]));
            g_users[i].active = false;
            if (g_user_count > 0) g_user_count--;
            return 0;
        }
    }
    return -1;
}

int auth_change_password(uint32_t uid, const char *new_password) {
    if (new_password == NULL) return -1;

    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].active && g_users[i].uid == uid) {
            auth_hash_password(new_password, g_users[i].password_hash);
            return 0;
        }
    }
    return -1;
}

/*--------------------------------------------------------------------------*/
/* Permission bits in file_mode (UNIX-style):                               */
/*   owner R/W = 0x100 / 0x080                                              */
/*   group R/W = 0x020 / 0x010                                              */
/*   other R/W = 0x004 / 0x002                                              */
/*--------------------------------------------------------------------------*/

bool auth_check_permission(uint32_t file_uid, uint32_t file_gid,
                           uint16_t file_mode, bool is_write) {
    if (g_current_uid < 0) return false;

    uint32_t cur_uid = (uint32_t)g_current_uid;
    if (cur_uid == 0U) return true;     /* root can do everything */

    /* Resolve the current user's gid from the account table. */
    uint32_t cur_gid = 0U;
    bool     found   = false;
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].active && g_users[i].uid == cur_uid) {
            cur_gid = g_users[i].gid;
            found   = true;
            break;
        }
    }
    if (!found) return false;

    uint16_t mask;
    if (cur_uid == file_uid) {
        mask = is_write ? (uint16_t)0x0080U : (uint16_t)0x0100U;
    } else if (cur_gid == file_gid) {
        mask = is_write ? (uint16_t)0x0010U : (uint16_t)0x0020U;
    } else {
        mask = is_write ? (uint16_t)0x0002U : (uint16_t)0x0004U;
    }
    return (uint16_t)(file_mode & mask) != (uint16_t)0;
}

const char *auth_username(uint32_t uid) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].active && g_users[i].uid == uid) {
            return g_users[i].username;
        }
    }
    return NULL;
}

/*--------------------------------------------------------------------------*/
/* Interactive login. Called from kernel_main before the shell starts.      */
/*--------------------------------------------------------------------------*/

void auth_run_login(void) {
    /* Reasonable input caps: usernames are bounded by USERNAME_MAX, and    */
    /* the password buffer is sized generously to allow longer passphrases  */
    /* than the resulting hash can distinguish.                             */
    char username[USERNAME_MAX];
    char password[64];

    console_printf("\n");
    for (int attempt = 0; attempt < 3; attempt++) {
        console_printf("ARES OS login: ");
        auth_read_line(username, sizeof(username), false);

        console_printf("Password: ");
        auth_read_line(password, sizeof(password), true);

        int uid = auth_login(username, password);

        /* Scrub the password buffer regardless of outcome. */
        auth_memset(password, 0, sizeof(password));

        if (uid >= 0) {
            g_current_uid = uid;
            const char *name = auth_username((uint32_t)uid);
            console_printf("Welcome, %s!\n", (name != NULL) ? name : username);
            return;
        }
        console_printf("Login incorrect\n\n");
    }

    console_printf("\nToo many failed attempts. System locked.\n");
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
