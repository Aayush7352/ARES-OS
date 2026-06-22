//==============================================================================
// ARES OS - VGA Text Mode Console Driver
//==============================================================================
// Provides screen I/O via the VGA text mode buffer at 0xB8000.
// The VGA text buffer is an array of 80x25 16-bit entries:
//   - Byte 0: ASCII character
//   - Byte 1: Attribute byte (foreground color in low nibble,
//              background color in high nibble)
//==============================================================================

#include "console.h"
#include <stdarg.h>

/* Current console state */
static console_t console = {
    .buffer = VGA_MEMORY,
    .row = 0,
    .col = 0,
    .color = 0x0F  /* White on black (default) */
};

/* Forward declaration for integer formatting helper */
static void print_dec(uint64_t value, int pad_zeros);

//==============================================================================
// Internal: Create a VGA entry from character and attribute
//==============================================================================
static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

//==============================================================================
// Initialize console
//==============================================================================
void console_init(void) {
    console.buffer = VGA_MEMORY;
    console.row = 0;
    console.col = 0;
    console.color = 0x0F;       /* White on black */
    console_clear();
}

//==============================================================================
// Clear the screen by filling with spaces
//==============================================================================
void console_clear(void) {
    uint16_t blank = vga_entry(' ', console.color);
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        console.buffer[i] = blank;
    }
    console.row = 0;
    console.col = 0;
}

//==============================================================================
// Set text color
//==============================================================================
void console_set_color(vga_color_t fg, vga_color_t bg) {
    console.color = (uint8_t)fg | ((uint8_t)bg << 4);
}

//==============================================================================
// Write a single character to the screen
//==============================================================================
void console_putchar(char c) {
    switch (c) {
    case '\n':                  /* Newline */
        console.row++;
        console.col = 0;
        break;

    case '\r':                  /* Carriage return */
        console.col = 0;
        break;

    case '\t':                  /* Tab - advance to next 8-column boundary */
        console.col = (console.col + 8) & (size_t)~7;
        break;

    case '\b':                  /* Backspace */
        if (console.col > 0) {
            console.col--;
            console.buffer[console.row * VGA_WIDTH + console.col]
                = vga_entry(' ', console.color);
        }
        break;

    default:                    /* Printable character */
        console.buffer[console.row * VGA_WIDTH + console.col]
            = vga_entry(c, console.color);
        console.col++;
        break;
    }

    /* Wrap to next line if at end of line */
    if (console.col >= VGA_WIDTH) {
        console.col = 0;
        console.row++;
    }

    /* Scroll if past last row */
    if (console.row >= VGA_HEIGHT) {
        console_scroll();
    }
}

//==============================================================================
// Write a null-terminated string
//==============================================================================
void console_puts(const char *str) {
    while (*str) {
        console_putchar(*str);
        str++;
    }
}

//==============================================================================
// Write a string followed by newline
//==============================================================================
void console_writeline(const char *str) {
    console_puts(str);
    console_putchar('\n');
}

//==============================================================================
// Simple printf-like formatter
// Supported: %s, %d, %u, %x, %X, %c, %%
//==============================================================================
void console_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            console_putchar(*p);
            continue;
        }

        p++;  /* Skip '%' */

        /* Check for format specifier */
        switch (*p) {
        case 's': {
            const char *s = va_arg(args, const char *);
            console_puts(s ? s : "(null)");
            break;
        }
        case 'd': {
            int32_t val = va_arg(args, int32_t);
            if (val < 0) {
                console_putchar('-');
                val = -val;
            }
            print_dec((uint64_t)val, 0);
            break;
        }
        case 'u': {
            uint32_t val = va_arg(args, uint32_t);
            print_dec((uint64_t)val, 0);
            break;
        }
        case 'x':
        case 'X': {
            uint32_t val = va_arg(args, uint32_t);
            console_puts("0x");
            for (int i = 7; i >= 0; i--) {
                uint8_t nibble = (val >> (i * 4)) & 0x0F;
                console_putchar("0123456789ABCDEF"[nibble]);
            }
            break;
        }
        case 'p': {
            uint64_t val = va_arg(args, uint64_t);
            console_puts("0x");
            for (int i = 15; i >= 0; i--) {
                uint8_t nibble = (val >> (i * 4)) & 0x0F;
                console_putchar("0123456789ABCDEF"[nibble]);
            }
            break;
        }
        case 'c': {
            int ch = va_arg(args, int);
            console_putchar((char)ch);
            break;
        }
        case '%':
            console_putchar('%');
            break;
        default:
            console_putchar('%');
            console_putchar(*p);
            break;
        }
    }

    va_end(args);
}

//==============================================================================
// Scroll the console up by one line
//==============================================================================
void console_scroll(void) {
    /* Move all rows up by one */
    for (size_t row = 0; row < VGA_HEIGHT - 1; row++) {
        for (size_t col = 0; col < VGA_WIDTH; col++) {
            console.buffer[row * VGA_WIDTH + col]
                = console.buffer[(row + 1) * VGA_WIDTH + col];
        }
    }

    /* Clear the last row */
    uint16_t blank = vga_entry(' ', console.color);
    size_t last_row = VGA_HEIGHT - 1;
    for (size_t col = 0; col < VGA_WIDTH; col++) {
        console.buffer[last_row * VGA_WIDTH + col] = blank;
    }

    console.row = VGA_HEIGHT - 1;
    console.col = 0;
}

//==============================================================================
// Set cursor position
//==============================================================================
void console_set_cursor(size_t row, size_t col) {
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        console.row = row;
        console.col = col;
    }
}

//==============================================================================
// Get current row
//==============================================================================
size_t console_get_row(void) {
    return console.row;
}

//==============================================================================
// Get current column
//==============================================================================
size_t console_get_col(void) {
    return console.col;
}

//==============================================================================
// Internal: Print an unsigned integer in decimal
//==============================================================================
static void print_dec(uint64_t value, int pad_zeros) {
    char buf[21];               /* Max 20 digits for uint64_t + null */
    int i = 20;
    buf[i] = '\0';

    do {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0);

    while (pad_zeros > 0 && (20 - i) < pad_zeros) {
        buf[--i] = '0';
    }

    console_puts(&buf[i]);
}
