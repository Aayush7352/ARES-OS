#ifndef AUTH_H
#define AUTH_H

#include <stdint.h>
#include <stdbool.h>

/*--------------------------------------------------------------------------*/
/* ARES OS - User authentication                                            */
/*                                                                          */
/* In-memory user account table backed by a tiny, NON-cryptographic XOR     */
/* hash. Suitable for the educational shell login prompt; not suitable for  */
/* anything that touches real user data.                                    */
/*--------------------------------------------------------------------------*/

#define MAX_USERS       16
#define USERNAME_MAX    24
#define PASS_HASH_LEN   32

typedef struct {
    char     username[USERNAME_MAX];
    uint8_t  password_hash[PASS_HASH_LEN]; /* Simple hash, not cryptographic */
    uint32_t uid;
    uint32_t gid;
    bool     is_root;
    bool     active;
} user_account_t;

/* Globals (declared extern here, defined in auth.c). */
extern user_account_t g_users[MAX_USERS];
extern int            g_user_count;
extern int            g_current_uid;    /* -1 = no user logged in */

/* Initialise with default root / admin / user accounts. */
void auth_init(void);

/* Authenticate user: returns uid on success, -1 on failure. */
int auth_login(const char *username, const char *password);

/* Create a new user account. Returns uid on success, -1 on failure. */
int auth_create_user(const char *username, const char *password, bool is_root);

/* Delete user account. Returns 0 on success, -1 on failure. */
int auth_delete_user(uint32_t uid);

/* Change password. Returns 0 on success, -1 on failure. */
int auth_change_password(uint32_t uid, const char *new_password);

/* Simple string hash (NOT cryptographic - for educational use). */
void auth_hash_password(const char *password, uint8_t *hash_out);

/* Check whether the currently logged-in user is allowed to access a file. */
bool auth_check_permission(uint32_t file_uid, uint32_t file_gid,
                           uint16_t file_mode, bool is_write);

/* Get the username for a uid, or NULL if no such active account exists. */
const char *auth_username(uint32_t uid);

/*--------------------------------------------------------------------------*/
/* Interactive login prompt. To be called from kernel_main before the shell */
/* starts. Allows up to 3 attempts; after the third failure the system is   */
/* locked (interrupts disabled, halt loop).                                 */
/*--------------------------------------------------------------------------*/
void auth_run_login(void);

#endif /* AUTH_H */
