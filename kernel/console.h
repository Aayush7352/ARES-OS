#ifndef ARES_CONSOLE_H
#define ARES_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* VGA text mode constants */
#define VGA_WIDTH       80
#define VGA_HEIGHT      25
#define VGA_MEMORY      ((volatile uint16_t *)0xB8000)

/* VGA colors */
typedef enum {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_LIGHT_BROWN   = 14,
    VGA_WHITE         = 15
} vga_color_t;

/* Console structure */
typedef struct {
    volatile uint16_t *buffer;
    size_t row;
    size_t col;
    uint8_t color;
} console_t;

/* Initialize console */
void console_init(void);

/* Clear the console screen */
void console_clear(void);

/* Set foreground and background colors */
void console_set_color(vga_color_t fg, vga_color_t bg);

/* Write a single character to current position */
void console_putchar(char c);

/* Write a null-terminated string */
void console_puts(const char *str);

/* Write a string with newline */
void console_writeline(const char *str);

/* Write a formatted string (simple printf-like) */
void console_printf(const char *fmt, ...);

/* Scroll the console up by one line */
void console_scroll(void);

/* Move cursor to specified position */
void console_set_cursor(size_t row, size_t col);

/* Get current row */
size_t console_get_row(void);

/* Get current column */
size_t console_get_col(void);

#endif /* ARES_CONSOLE_H */
