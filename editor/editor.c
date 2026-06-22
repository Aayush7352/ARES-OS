/*==========================================================================*/
/* ARES OS - Text Editor                                                      */
/*                                                                            */
/* A minimal nano-style full-screen editor for ARES OS. The editor owns the   */
/* 80x25 VGA text buffer for the duration of `editor_open` and returns to     */
/* the shell when the user presses Ctrl+X or types `:q` in the mini command   */
/* line. File I/O goes through the syscall layer (INT 0x80) so the editor     */
/* exercises the same path a user-mode program would once ring 3 lands.       */
/*                                                                            */
/* Layout:                                                                    */
/*   Rows 0..23   File content, optionally scrolled vertically.               */
/*   Row 24       Status bar (filename, dirty flag, line/col, hint).          */
/*==========================================================================*/

#include <stdint.h>
#include <stddef.h>

#include "editor.h"
#include "../kernel/console.h"
#include "../drivers/keyboard.h"
#include "../process/process.h"
#include "../fs/aresfs.h"
#include "../syscall/syscall.h"

/*--------------------------------------------------------------------------*/
/* Local string helpers (the kernel ships no libc).                         */
/*--------------------------------------------------------------------------*/

static size_t ed_strlen(const char *s) {
    size_t n = 0;
    while (s != NULL && s[n] != '\0') n++;
    return n;
}

static int ed_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

static void ed_strcpy_n(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (cap == 0U) return;
    while (i + 1U < cap && src != NULL && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Shift `count` bytes inside `buf` starting at `src` to `dst`. Handles
 * overlap in both directions (memmove semantics). */
static void ed_memmove(char *dst, const char *src, size_t count) {
    if (dst == src || count == 0U) return;
    if (dst < src) {
        for (size_t i = 0; i < count; i++) dst[i] = src[i];
    } else {
        for (size_t i = count; i > 0U; i--) dst[i - 1U] = src[i - 1U];
    }
}

/*--------------------------------------------------------------------------*/
/* Syscall wrappers — issue INT 0x80 so file I/O exercises the kernel ABI.  */
/*--------------------------------------------------------------------------*/

static int64_t ed_syscall3(uint64_t num,
                           uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                     : "memory");
    return ret;
}

static int ed_sys_open(const char *path, int flags) {
    return (int)ed_syscall3((uint64_t)SYS_open,
                            (uint64_t)(uintptr_t)path,
                            (uint64_t)(int64_t)flags, 0U);
}

static int ed_sys_read(int fd, void *buf, size_t count) {
    return (int)ed_syscall3((uint64_t)SYS_read,
                            (uint64_t)(int64_t)fd,
                            (uint64_t)(uintptr_t)buf,
                            (uint64_t)count);
}

static int ed_sys_write(int fd, const void *buf, size_t count) {
    return (int)ed_syscall3((uint64_t)SYS_write,
                            (uint64_t)(int64_t)fd,
                            (uint64_t)(uintptr_t)buf,
                            (uint64_t)count);
}

static void ed_sys_close(int fd) {
    (void)ed_syscall3((uint64_t)SYS_close,
                      (uint64_t)(int64_t)fd, 0U, 0U);
}

/*--------------------------------------------------------------------------*/
/* Editor state — everything lives in BSS, no heap usage.                   */
/*--------------------------------------------------------------------------*/

static char     g_buf[EDITOR_BUFFER_SIZE];   /* In-memory file contents     */
static size_t   g_len;                       /* Used bytes in g_buf         */
static size_t   g_cursor;                    /* Byte offset of cursor       */
static size_t   g_view_off;                  /* Offset of top visible line  */
static char     g_path[EDITOR_FILENAME_MAX]; /* Current file path           */
static uint8_t  g_dirty;                     /* 1 if unsaved changes        */
static char     g_status_msg[80];            /* Transient status text       */

/*--------------------------------------------------------------------------*/
/* Direct VGA helpers — bypass console_putchar so we can write to any cell. */
/*--------------------------------------------------------------------------*/

static void ed_vga_put(size_t row, size_t col, char c, uint8_t attr) {
    if (row >= EDITOR_SCREEN_ROWS || col >= EDITOR_SCREEN_COLS) return;
    volatile uint16_t *cell =
        &VGA_MEMORY[row * EDITOR_SCREEN_COLS + col];
    *cell = (uint16_t)((uint16_t)(uint8_t)c
                       | ((uint16_t)attr << 8));
}

static void ed_clear_row(size_t row, uint8_t attr) {
    for (size_t col = 0; col < EDITOR_SCREEN_COLS; col++) {
        ed_vga_put(row, col, ' ', attr);
    }
}

static void ed_clear_text_area(void) {
    uint8_t attr = (uint8_t)VGA_WHITE | ((uint8_t)VGA_BLACK << 4);
    for (size_t r = 0; r < EDITOR_TEXT_ROWS; r++) ed_clear_row(r, attr);
}

/*--------------------------------------------------------------------------*/
/* Buffer geometry — convert byte offsets to file-relative line/column.     */
/*--------------------------------------------------------------------------*/

static size_t ed_line_start(size_t off) {
    while (off > 0U && g_buf[off - 1U] != '\n') off--;
    return off;
}

static size_t ed_line_end(size_t off) {
    while (off < g_len && g_buf[off] != '\n') off++;
    return off;
}

static size_t ed_prev_line(size_t off) {
    size_t start = ed_line_start(off);
    if (start == 0U) return 0U;
    return ed_line_start(start - 1U);
}

static size_t ed_next_line(size_t off) {
    size_t end = ed_line_end(off);
    if (end >= g_len) return g_len;
    return end + 1U;
}

static size_t ed_col_at(size_t off) {
    return off - ed_line_start(off);
}

/* Count visible-line position of `off` relative to view_off. Returns -1 if
 * the offset is above the viewport, EDITOR_TEXT_ROWS if it is below. */
static int ed_screen_row(size_t off) {
    if (off < g_view_off) return -1;
    size_t row = 0;
    size_t i = g_view_off;
    while (i < off) {
        if (g_buf[i] == '\n') row++;
        if (row >= EDITOR_TEXT_ROWS) return (int)EDITOR_TEXT_ROWS;
        i++;
    }
    return (int)row;
}

/*--------------------------------------------------------------------------*/
/* Status bar / rendering.                                                  */
/*--------------------------------------------------------------------------*/

static void ed_set_status(const char *msg) {
    ed_strcpy_n(g_status_msg, msg, sizeof(g_status_msg));
}

static void ed_draw_status_bar(void) {
    uint8_t attr = (uint8_t)VGA_BLACK | ((uint8_t)VGA_LIGHT_GREY << 4);
    ed_clear_row(EDITOR_STATUS_ROW, attr);

    /* Filename + dirty marker */
    size_t col = 1U;
    const char *name = g_path[0] == '\0' ? "[no-name]" : g_path;
    for (size_t i = 0; name[i] != '\0' && col < EDITOR_SCREEN_COLS; i++) {
        ed_vga_put(EDITOR_STATUS_ROW, col, name[i], attr);
        col++;
    }
    if (g_dirty != 0U && col + 4U < EDITOR_SCREEN_COLS) {
        ed_vga_put(EDITOR_STATUS_ROW, col++, ' ', attr);
        ed_vga_put(EDITOR_STATUS_ROW, col++, '[', attr);
        ed_vga_put(EDITOR_STATUS_ROW, col++, '+', attr);
        ed_vga_put(EDITOR_STATUS_ROW, col++, ']', attr);
    }

    /* Right side: hint / line:col indicator */
    const char *hint = "^S save ^X exit ESC cmd";
    size_t hlen = ed_strlen(hint);
    size_t hcol = (EDITOR_SCREEN_COLS > hlen + 1U)
                  ? EDITOR_SCREEN_COLS - hlen - 1U : 0U;
    for (size_t i = 0; i < hlen; i++) {
        ed_vga_put(EDITOR_STATUS_ROW, hcol + i, hint[i], attr);
    }

    /* Transient message, if present, replaces the right-hand hint. */
    if (g_status_msg[0] != '\0') {
        size_t mlen = ed_strlen(g_status_msg);
        size_t mcol = (EDITOR_SCREEN_COLS > mlen + 1U)
                      ? EDITOR_SCREEN_COLS - mlen - 1U : 0U;
        for (size_t i = 0; i < EDITOR_SCREEN_COLS - mcol - 1U; i++) {
            ed_vga_put(EDITOR_STATUS_ROW, mcol + i, ' ', attr);
        }
        for (size_t i = 0; i < mlen; i++) {
            ed_vga_put(EDITOR_STATUS_ROW, mcol + i, g_status_msg[i], attr);
        }
    }
}

static void ed_render(void) {
    ed_clear_text_area();
    uint8_t attr = (uint8_t)VGA_WHITE | ((uint8_t)VGA_BLACK << 4);

    size_t row = 0;
    size_t col = 0;
    size_t i = g_view_off;
    while (i < g_len && row < EDITOR_TEXT_ROWS) {
        char c = g_buf[i];
        if (c == '\n') {
            row++;
            col = 0;
        } else if (col < EDITOR_SCREEN_COLS) {
            ed_vga_put(row, col, c, attr);
            col++;
        }
        i++;
    }

    ed_draw_status_bar();

    /* Place hardware cursor at the visible cursor position. */
    int srow = ed_screen_row(g_cursor);
    size_t scol = ed_col_at(g_cursor);
    if (scol >= EDITOR_SCREEN_COLS) scol = EDITOR_SCREEN_COLS - 1U;
    if (srow < 0) srow = 0;
    if (srow >= (int)EDITOR_TEXT_ROWS) srow = (int)EDITOR_TEXT_ROWS - 1;
    console_set_cursor((size_t)srow, scol);
}

/* Ensure the cursor is on-screen by shifting view_off when needed. */
static void ed_scroll_into_view(void) {
    while (g_cursor < g_view_off) {
        g_view_off = ed_prev_line(g_view_off);
    }
    while (ed_screen_row(g_cursor) >= (int)EDITOR_TEXT_ROWS) {
        g_view_off = ed_next_line(g_view_off);
        if (g_view_off >= g_len) break;
    }
}

/*--------------------------------------------------------------------------*/
/* Buffer mutations.                                                        */
/*--------------------------------------------------------------------------*/

static void ed_insert_char(char c) {
    if (g_len + 1U >= EDITOR_BUFFER_SIZE) {
        ed_set_status("buffer full");
        return;
    }
    ed_memmove(&g_buf[g_cursor + 1U], &g_buf[g_cursor], g_len - g_cursor);
    g_buf[g_cursor] = c;
    g_len++;
    g_cursor++;
    g_dirty = 1U;
}

static void ed_delete_before(void) {
    if (g_cursor == 0U) return;
    ed_memmove(&g_buf[g_cursor - 1U], &g_buf[g_cursor], g_len - g_cursor);
    g_len--;
    g_cursor--;
    g_dirty = 1U;
}

static void ed_move_left(void) {
    if (g_cursor > 0U) g_cursor--;
}

static void ed_move_right(void) {
    if (g_cursor < g_len) g_cursor++;
}

static void ed_move_up(void) {
    size_t col = ed_col_at(g_cursor);
    size_t prev = ed_prev_line(g_cursor);
    if (prev == ed_line_start(g_cursor)) return;          /* first line */
    size_t prev_end = ed_line_end(prev);
    size_t prev_len = prev_end - prev;
    g_cursor = prev + (col < prev_len ? col : prev_len);
}

static void ed_move_down(void) {
    size_t col = ed_col_at(g_cursor);
    size_t next = ed_next_line(g_cursor);
    if (next >= g_len) {
        size_t last_end = ed_line_end(g_cursor);
        size_t last_len = last_end - ed_line_start(g_cursor);
        if (col < last_len) g_cursor = ed_line_start(g_cursor) + col;
        else g_cursor = last_end;
        return;
    }
    size_t next_end = ed_line_end(next);
    size_t next_len = next_end - next;
    g_cursor = next + (col < next_len ? col : next_len);
}

/*--------------------------------------------------------------------------*/
/* File I/O.                                                                */
/*--------------------------------------------------------------------------*/

static void ed_load_file(const char *filename) {
    g_len = 0;
    g_cursor = 0;
    g_view_off = 0;
    g_dirty = 0;

    if (filename == NULL || filename[0] == '\0') {
        ed_set_status("new file");
        return;
    }

    int fd = ed_sys_open(filename, ARESFS_O_RDONLY);
    if (fd < 0) {
        ed_set_status("new file");
        return;
    }

    int n = ed_sys_read(fd, g_buf, EDITOR_BUFFER_SIZE - 1U);
    ed_sys_close(fd);

    if (n > 0) g_len = (size_t)n;
    ed_set_status("loaded");
}

static void ed_save_file(void) {
    if (g_path[0] == '\0') {
        ed_set_status("no filename — use :w <name>");
        return;
    }
    int fd = ed_sys_open(g_path,
                         ARESFS_O_WRONLY | ARESFS_O_CREAT | ARESFS_O_TRUNC);
    if (fd < 0) {
        ed_set_status("save failed (open)");
        return;
    }
    int n = ed_sys_write(fd, g_buf, g_len);
    ed_sys_close(fd);
    if (n < 0 || (size_t)n != g_len) {
        ed_set_status("save failed (write)");
        return;
    }
    g_dirty = 0;
    ed_set_status("saved");
}

/*--------------------------------------------------------------------------*/
/* Mini command line — invoked on ESC. Supports :q, :w, :wq, :w <name>.     */
/*--------------------------------------------------------------------------*/

static int ed_read_command(char *out, size_t cap) {
    uint8_t attr = (uint8_t)VGA_WHITE | ((uint8_t)VGA_BLUE << 4);
    ed_clear_row(EDITOR_STATUS_ROW, attr);
    ed_vga_put(EDITOR_STATUS_ROW, 0, ':', attr);
    console_set_cursor(EDITOR_STATUS_ROW, 1U);

    size_t pos = 0;
    while (1) {
        __asm__ volatile("hlt");
        while (keyboard_has_data() != 0U) {
            uint8_t c = keyboard_getchar();
            if (c == 0U) continue;
            if (c == KEY_ENTER) {
                out[pos] = '\0';
                return (int)pos;
            }
            if (c == KEY_ESC) {
                out[0] = '\0';
                return -1;
            }
            if (c == KEY_BACKSPACE) {
                if (pos > 0U) {
                    pos--;
                    ed_vga_put(EDITOR_STATUS_ROW, pos + 1U, ' ', attr);
                    console_set_cursor(EDITOR_STATUS_ROW, pos + 1U);
                }
                continue;
            }
            if (c >= 0x20U && c < 0x7FU && pos + 1U < cap) {
                out[pos] = (char)c;
                ed_vga_put(EDITOR_STATUS_ROW, pos + 1U, (char)c, attr);
                pos++;
                console_set_cursor(EDITOR_STATUS_ROW, pos + 1U);
            }
        }
    }
}

/* Returns 1 if the command requests exit, 0 otherwise. */
static int ed_run_command(void) {
    char cmd[EDITOR_CMD_MAX];
    int len = ed_read_command(cmd, sizeof(cmd));
    if (len <= 0) return 0;

    if (ed_strcmp(cmd, "q") == 0) {
        if (g_dirty != 0U) {
            ed_set_status("unsaved changes — use :q! or :wq");
            return 0;
        }
        return 1;
    }
    if (ed_strcmp(cmd, "q!") == 0) return 1;
    if (ed_strcmp(cmd, "w") == 0)  { ed_save_file(); return 0; }
    if (ed_strcmp(cmd, "wq") == 0) {
        ed_save_file();
        return g_dirty == 0U ? 1 : 0;
    }
    if (cmd[0] == 'w' && cmd[1] == ' ') {
        ed_strcpy_n(g_path, &cmd[2], sizeof(g_path));
        ed_save_file();
        return 0;
    }
    ed_set_status("unknown command");
    return 0;
}

/*--------------------------------------------------------------------------*/
/* Main interactive loop.                                                   */
/*--------------------------------------------------------------------------*/

void editor_open(const char *filename) {
    ed_strcpy_n(g_path, filename, sizeof(g_path));
    g_status_msg[0] = '\0';
    ed_load_file(g_path);

    console_clear();
    ed_render();

    while (1) {
        __asm__ volatile("hlt");
        while (keyboard_has_data() != 0U) {
            uint8_t c = keyboard_getchar();
            if (c == 0U) continue;

            uint8_t mods = keyboard_get_modifiers();
            uint8_t ctrl = (uint8_t)(mods
                                     & (KBD_MOD_LCTRL | KBD_MOD_RCTRL));

            /* Drop standalone modifier scancodes — we only care about the
             * non-modifier key that follows. */
            if (c == KEY_LCTRL || c == KEY_RCTRL
                || c == KEY_LSHIFT || c == KEY_RSHIFT
                || c == KEY_LALT || c == KEY_RALT) {
                continue;
            }

            if (ctrl != 0U && (c == 's' || c == 'S')) {
                ed_save_file();
            } else if (ctrl != 0U && (c == 'x' || c == 'X')) {
                console_clear();
                return;
            } else if (c == KEY_ESC) {
                if (ed_run_command() != 0) { console_clear(); return; }
            } else if (c == KEY_UP)    { ed_move_up();    g_status_msg[0]='\0'; }
            else if  (c == KEY_DOWN)   { ed_move_down();  g_status_msg[0]='\0'; }
            else if  (c == KEY_LEFT)   { ed_move_left();  g_status_msg[0]='\0'; }
            else if  (c == KEY_RIGHT)  { ed_move_right(); g_status_msg[0]='\0'; }
            else if  (c == KEY_BACKSPACE) { ed_delete_before(); }
            else if  (c == KEY_ENTER)  { ed_insert_char('\n'); }
            else if  (c == KEY_TAB)    { ed_insert_char(' '); ed_insert_char(' '); }
            else if  (c >= 0x20U && c < 0x7FU) { ed_insert_char((char)c); }
            /* Anything else (F-keys, page-up/down, etc.) is ignored. */

            ed_scroll_into_view();
            ed_render();
        }
    }
}
