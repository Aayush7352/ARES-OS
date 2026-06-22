/*==========================================================================*/
/* ARES OS - User Shell                                                       */
/*                                                                            */
/* A minimal UNIX-style command shell. Reads a line from the keyboard         */
/* driver, supports backspace editing and history (UP/DOWN arrows), parses    */
/* the input into argc/argv tokens and dispatches to a static table of        */
/* built-in commands.                                                         */
/*==========================================================================*/

#include <stdint.h>
#include <stddef.h>

#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "rtc.h"
#include "process.h"
#include "pcb.h"
#include "pmm.h"
#include "heap.h"
#include "irq.h"
#include "editor.h"
#include "../calc/calc.h"
#include "test_runner.h"
#include "observability.h"
#include "benchmark.h"
#include "auth.h"

/*--------------------------------------------------------------------------*/
/* Local string helpers (no libc available in freestanding kernel).         */
/*--------------------------------------------------------------------------*/

static size_t sh_strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int sh_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/* Copy up to (cap - 1) bytes from src to dst and NUL-terminate. */
static void sh_strcpy_n(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0) return;
    while (i + 1U < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/*--------------------------------------------------------------------------*/
/* Command history (circular buffer).                                       */
/*--------------------------------------------------------------------------*/

static char history[SHELL_HISTORY_SIZE][SHELL_MAX_LINE];
static int  history_count = 0;   /* Valid entries (0..SHELL_HISTORY_SIZE). */
static int  history_head  = 0;   /* Next slot to write (circular index).   */
static int  history_nav   = -1;  /* -1 = not navigating; 0 = newest, etc.  */

/* Resolve navigation offset `nav` (0 = newest) to a backing array index. */
static int history_index_at(int nav) {
    int idx = history_head - 1 - nav;
    idx = (idx % SHELL_HISTORY_SIZE + SHELL_HISTORY_SIZE) % SHELL_HISTORY_SIZE;
    return idx;
}

static void history_push(const char *line) {
    /* Don't store blanks or exact duplicates of the most recent entry. */
    if (line[0] == '\0') return;
    if (history_count > 0) {
        int newest = history_index_at(0);
        if (sh_strcmp(history[newest], line) == 0) return;
    }
    sh_strcpy_n(history[history_head], line, SHELL_MAX_LINE);
    history_head = (history_head + 1) % SHELL_HISTORY_SIZE;
    if (history_count < SHELL_HISTORY_SIZE) history_count++;
}

/*--------------------------------------------------------------------------*/
/* Line editing helpers.                                                    */
/*--------------------------------------------------------------------------*/

/* Replace the visible line and the buffer with `src`. `pos` tracks the
 * current buffer length so we know how many characters to erase first. */
static int line_replace(char *line, int pos, const char *src) {
    /* Erase the existing line: backspace, overwrite with spaces, backspace. */
    for (int i = 0; i < pos; i++) console_putchar('\b');
    for (int i = 0; i < pos; i++) console_putchar(' ');
    for (int i = 0; i < pos; i++) console_putchar('\b');

    /* Copy new content and echo it. */
    sh_strcpy_n(line, src, SHELL_MAX_LINE);
    int new_len = (int)sh_strlen(line);
    for (int i = 0; i < new_len; i++) console_putchar(line[i]);
    return new_len;
}

/*--------------------------------------------------------------------------*/
/* Command parser: tokenize on whitespace into argv[].                      */
/*--------------------------------------------------------------------------*/

static int shell_parse(char *line, char **argv, int max_args) {
    int argc = 0;
    int i = 0;

    while (line[i] != '\0' && argc < max_args) {
        /* Skip leading whitespace. */
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (line[i] == '\0') break;

        /* Start of a token. */
        argv[argc++] = &line[i];

        /* Advance to end of token. */
        while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t') i++;

        /* NUL-terminate the token. */
        if (line[i] != '\0') {
            line[i] = '\0';
            i++;
        }
    }
    return argc;
}

/*--------------------------------------------------------------------------*/
/* Built-in command handlers.                                               */
/*--------------------------------------------------------------------------*/

static int cmd_help(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_ps(int argc, char **argv);
static int cmd_date(int argc, char **argv);
static int cmd_meminfo(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_shutdown(int argc, char **argv);
static int cmd_uptime(int argc, char **argv);
static int cmd_editor(int argc, char **argv);
static int cmd_calc(int argc, char **argv);
static int cmd_test(int argc, char **argv);
static int cmd_top(int argc, char **argv);
static int cmd_bench(int argc, char **argv);
static int cmd_users(int argc, char **argv);
static int cmd_login(int argc, char **argv);
static int cmd_log(int argc, char **argv);

static int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    console_printf("Built-in commands:\n");
    console_printf("  help            Show this list\n");
    console_printf("  echo [args...]  Print arguments\n");
    console_printf("  clear           Clear the screen\n");
    console_printf("  ps              List active processes\n");
    console_printf("  date            Show current date/time (RTC)\n");
    console_printf("  meminfo         Show memory statistics\n");
    console_printf("  uptime          Show ticks since boot\n");
    console_printf("  reboot          Reboot the machine (stub)\n");
    console_printf("  shutdown        Power off the machine (stub)\n");
    return 0;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        console_printf("%s", argv[i]);
        if (i + 1 < argc) console_putchar(' ');
    }
    console_putchar('\n');
    return 0;
}

static int cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    console_clear();
    return 0;
}

static int cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;

    console_printf("%-4s %-24s %-10s %-8s %-8s\n",
                   "PID", "NAME", "STATE", "PRIO", "TICKS");
    console_printf("%-4s %-24s %-10s %-8s %-8s\n",
                   "---", "----", "-----", "----", "-----");

    if (current_process) {
        console_printf("%-4u %-24s %-10s %-8s %-8u (current)\n",
                       current_process->pid,
                       current_process->name,
                       process_state_str(current_process->state),
                       process_priority_str(
                           (enum process_priority)current_process->priority),
                       current_process->total_ticks);
    }

    pcb_t *buf[MAX_PROCESSES];
    int count = pcb_enum_active(buf, MAX_PROCESSES);
    for (int i = 0; i < count; i++) {
        if (buf[i] == current_process) continue;
        console_printf("%-4u %-24s %-10s %-8s %-8u\n",
                       buf[i]->pid,
                       buf[i]->name,
                       process_state_str(buf[i]->state),
                       process_priority_str(
                           (enum process_priority)buf[i]->priority),
                       buf[i]->total_ticks);
    }
    console_printf("(%u active)\n", (unsigned)process_count);
    return 0;
}

static int cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    char buf[32];
    rtc_format_time(buf, (uint32_t)sizeof(buf));
    console_printf("Time: %s\n", buf);
    return 0;
}

static int cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;

    uint64_t free_pages = pmm_get_free_count();
    uint64_t used_pages = pmm_get_used_count();
    uint64_t total_pages = free_pages + used_pages;

    /* Each page is 4 KiB. */
    uint64_t total_kib = total_pages * 4U;
    uint64_t used_kib  = used_pages  * 4U;
    uint64_t free_kib  = free_pages  * 4U;

    console_printf("Physical memory (4 KiB pages):\n");
    console_printf("  total : %lu pages (%lu KiB)\n",
                   (unsigned long)total_pages, (unsigned long)total_kib);
    console_printf("  used  : %lu pages (%lu KiB)\n",
                   (unsigned long)used_pages,  (unsigned long)used_kib);
    console_printf("  free  : %lu pages (%lu KiB)\n",
                   (unsigned long)free_pages,  (unsigned long)free_kib);

    uint64_t heap_used = 0;
    uint64_t heap_free = 0;
    uint64_t heap_largest = 0;
    heap_stats(&heap_used, &heap_free, &heap_largest);

    console_printf("Kernel heap:\n");
    console_printf("  used    : %lu bytes\n", (unsigned long)heap_used);
    console_printf("  free    : %lu bytes\n", (unsigned long)heap_free);
    console_printf("  largest : %lu bytes\n", (unsigned long)heap_largest);
    return 0;
}

static int cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    console_printf("Uptime: %lu ticks\n", (unsigned long)timer_ticks);
    return 0;
}

static int cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    console_printf("reboot: Not implemented yet\n");
    return 0;
}

static int cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    console_printf("shutdown: Not implemented yet\n");
    return 0;
}

static int cmd_editor(int argc, char **argv) {
    if (argc < 2) { console_printf("usage: editor <filename>\n"); return 0; }
    editor_open(argv[1]);
    return 0;
}

static int cmd_calc(int argc, char **argv) {
    (void)argc; (void)argv;
    calc_run();
    return 0;
}

static int cmd_test(int argc, char **argv) {
    (void)argc; (void)argv;
    test_run_all();
    return 0;
}

static int cmd_top(int argc, char **argv) {
    (void)argc; (void)argv;
    top_display();
    return 0;
}

static int cmd_bench(int argc, char **argv) {
    (void)argc; (void)argv;
    bench_run_all();
    return 0;
}

static int cmd_users(int argc, char **argv) {
    (void)argc; (void)argv;
    console_printf("Registered users:\n");
    console_printf("%-24s %-6s %-6s %-5s\n", "USERNAME", "UID", "GID", "ROOT");
    for (int i = 0; i < MAX_USERS; i++) {
        if (g_users[i].active) {
            console_printf("%-24s %-6u %-6u %-5s\n",
                           g_users[i].username,
                           g_users[i].uid, g_users[i].gid,
                           g_users[i].is_root ? "yes" : "no");
        }
    }
    console_printf("Total: %d user(s)\n\n", g_user_count);
    if (g_current_uid < 0) {
        console_printf("No user currently logged in.\n");
    } else {
        const char *uname = auth_username((uint32_t)g_current_uid);
        console_printf("Logged in as: %s (UID %d)\n",
                       uname ? uname : "?", g_current_uid);
    }
    return 0;
}

static int cmd_login(int argc, char **argv) {
    (void)argc; (void)argv;
    auth_run_login();
    return 0;
}

static int cmd_log(int argc, char **argv) {
    (void)argc; (void)argv;
    size_t len = 0;
    const char *buf = log_read(&len);
    console_printf("--- Kernel Log (%lu bytes) ---\n", (unsigned long)len);
    console_printf("%s\n", buf);
    return 0;
}

/*--------------------------------------------------------------------------*/
/* Dispatch: look up the command name in a small table.                     */
/*--------------------------------------------------------------------------*/

typedef int (*cmd_fn_t)(int argc, char **argv);

struct command {
    const char *name;
    cmd_fn_t    fn;
};

static const struct command g_commands[] = {
    { "help",     cmd_help     },
    { "echo",     cmd_echo     },
    { "clear",    cmd_clear    },
    { "ps",       cmd_ps       },
    { "date",     cmd_date     },
    { "meminfo",  cmd_meminfo  },
    { "uptime",   cmd_uptime   },
    { "reboot",   cmd_reboot   },
    { "shutdown", cmd_shutdown },
    { "editor",   cmd_editor   },
    { "edit",     cmd_editor   },
    { "calc",     cmd_calc     },
    { "test",     cmd_test     },
    { "top",      cmd_top      },
    { "bench",    cmd_bench    },
    { "users",    cmd_users    },
    { "login",    cmd_login    },
    { "log",      cmd_log      },
    { NULL,       NULL         },
};

static void shell_execute(char *line) {
    char *argv[SHELL_MAX_ARGS];
    int argc = shell_parse(line, argv, SHELL_MAX_ARGS);
    if (argc == 0) return;

    for (int i = 0; g_commands[i].name != NULL; i++) {
        if (sh_strcmp(argv[0], g_commands[i].name) == 0) {
            (void)g_commands[i].fn(argc, argv);
            return;
        }
    }
    console_printf("sh: %s: command not found\n", argv[0]);
}

/*--------------------------------------------------------------------------*/
/* Main interactive loop.                                                   */
/*--------------------------------------------------------------------------*/

void shell_main(void) {
    char line[SHELL_MAX_LINE];
    int  pos = 0;

    console_set_color(VGA_WHITE, VGA_BLACK);
    console_printf("\nARES OS v0.1.0 - Type 'help' for commands\n\n");

    while (1) {
        /* Print prompt. */
        console_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        console_printf("ares@aresos:~$ ");
        console_set_color(VGA_WHITE, VGA_BLACK);

        pos = 0;
        line[0] = '\0';
        history_nav = -1;

        /* Read one line. */
        while (1) {
            __asm__ volatile("hlt");

            while (keyboard_has_data()) {
                uint8_t c = keyboard_getchar();
                if (c == 0) continue;

                if (c == KEY_ENTER) {
                    console_putchar('\n');
                    goto line_done;
                } else if (c == KEY_BACKSPACE) {
                    if (pos > 0) {
                        pos--;
                        line[pos] = '\0';
                        /* VGA backspace: move back, blank, back again. */
                        console_putchar('\b');
                        console_putchar(' ');
                        console_putchar('\b');
                    }
                } else if (c == KEY_UP) {
                    if (history_count > 0
                        && history_nav < history_count - 1) {
                        history_nav++;
                        int idx = history_index_at(history_nav);
                        pos = line_replace(line, pos, history[idx]);
                    }
                } else if (c == KEY_DOWN) {
                    if (history_nav > 0) {
                        history_nav--;
                        int idx = history_index_at(history_nav);
                        pos = line_replace(line, pos, history[idx]);
                    } else if (history_nav == 0) {
                        history_nav = -1;
                        pos = line_replace(line, pos, "");
                    }
                } else if (c == KEY_TAB) {
                    /* Render TAB as up to 4 spaces. */
                    for (int i = 0;
                         i < 4 && pos < SHELL_MAX_LINE - 1; i++) {
                        line[pos++] = ' ';
                        console_putchar(' ');
                    }
                } else if (c >= 0x20 && c < 0x7F
                           && pos < SHELL_MAX_LINE - 1) {
                    line[pos++] = (char)c;
                    console_putchar((char)c);
                }
                /* Other special keys (ESC, F-keys, arrows L/R, ...) ignored. */
            }
        }
    line_done:
        line[pos] = '\0';

        if (pos == 0) continue;

        history_push(line);
        shell_execute(line);
    }
}
