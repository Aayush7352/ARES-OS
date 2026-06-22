#include <stddef.h>
#include "keyboard.h"
#include "../lib/io.h"

/*==============================================================================
 * PS/2 Keyboard Driver for ARES OS
 *
 * Translates scancode set 1 (the default for PS/2 controllers in QEMU and
 * most PC hardware) into ASCII characters.  Supports the US QWERTY layout.
 * Modifier keys (Shift, Ctrl, Alt, GUI) are tracked separately.
 * Extended scancodes (0xE0 prefix) are handled for arrow keys, etc.
 *==============================================================================
 *
 * Scancode Format (Set 1):
 *   Make  (key pressed):  0x01–0x58        — single byte
 *                          0xE0, 0x??      — two-byte extended
 *   Break (key released): 0x81–0xD8        — make | 0x80
 *                          0xE0, 0x??|0x80 — extended break
 *
 * The keyboard controller (8042) communicates via ports 0x60 (data) and
 * 0x64 (status/command).
 *==============================================================================
 *
 * Layout tables:
 *   scancode_us[MAX_SCAN] — maps scancode indexes to ASCII (no shift)
 *   scancode_us_shift[]   — maps scancode indexes to ASCII (shift pressed)
 *   Special keys (F1–F12, arrows, etc.) are mapped to KEY_* constants.
 *==============================================================================*/

/*==============================================================================
 * Constants
 *==============================================================================*/
#define MAX_SCAN  0x60   /* scancodes 0x00-0x5F are valid in set 1 */

/*==============================================================================
 * State
 *==============================================================================*/
static uint8_t modifiers;
static uint8_t kbd_buffer[KBD_BUF_SIZE];
static volatile uint16_t buf_head;  /* write index */
static volatile uint16_t buf_tail;  /* read index */
static uint8_t extended;            /* 1 if last byte was 0xE0 prefix */

/*==============================================================================
 * US QWERTY Scancode → ASCII Tables
 *
 * Indexed by scancode.  0x00 = no mapping (key released or unmapped).
 * Special keys (KEY_UP, KEY_F1, etc.) are from keyboard.h constants.
 *==============================================================================*/

static const uint8_t keymap_us[MAX_SCAN] = {
    /* 0x00 */ KEY_NONE,   KEY_ESC,     '1',         '2',
    /* 0x04 */ '3',         '4',         '5',         '6',
    /* 0x08 */ '7',         '8',         '9',         '0',
    /* 0x0C */ '-',         '=',         KEY_BACKSPACE, KEY_TAB,
    /* 0x10 */ 'q',         'w',         'e',         'r',
    /* 0x14 */ 't',         'y',         'u',         'i',
    /* 0x18 */ 'o',         'p',         '[',         ']',
    /* 0x1C */ KEY_ENTER,  KEY_LCTRL,   'a',         's',
    /* 0x20 */ 'd',         'f',         'g',         'h',
    /* 0x24 */ 'j',         'k',         'l',         ';',
    /* 0x28 */ '\'',        '`',         KEY_LSHIFT,  '\\',
    /* 0x2C */ 'z',         'x',         'c',         'v',
    /* 0x30 */ 'b',         'n',         'm',         ',',
    /* 0x34 */ '.',         '/',         KEY_RSHIFT,  '*',
    /* 0x38 */ KEY_LALT,   ' ',         KEY_CAPS,    KEY_F1,
    /* 0x3C */ KEY_F2,     KEY_F3,      KEY_F4,      KEY_F5,
    /* 0x40 */ KEY_F6,     KEY_F7,      KEY_F8,      KEY_F9,
    /* 0x44 */ KEY_F10,    KEY_NUMLOCK, KEY_SCROLLLOCK, '7',
    /* 0x48 */ '8',         '9',         '-',         '4',
    /* 0x4C */ '5',         '6',         '+',         '1',
    /* 0x50 */ '2',         '3',         '0',         '.',
    /* 0x54 */ KEY_NONE,   KEY_NONE,    KEY_NONE,    KEY_F11,
    /* 0x58 */ KEY_F12,
};

static const uint8_t keymap_us_shift[MAX_SCAN] = {
    /* 0x00 */ KEY_NONE,   KEY_ESC,     '!',         '@',
    /* 0x04 */ '#',         '$',         '%',         '^',
    /* 0x08 */ '&',         '*',         '(',         ')',
    /* 0x0C */ '_',         '+',         KEY_BACKSPACE, KEY_TAB,
    /* 0x10 */ 'Q',         'W',         'E',         'R',
    /* 0x14 */ 'T',         'Y',         'U',         'I',
    /* 0x18 */ 'O',         'P',         '{',         '}',
    /* 0x1C */ KEY_ENTER,  KEY_LCTRL,   'A',         'S',
    /* 0x20 */ 'D',         'F',         'G',         'H',
    /* 0x24 */ 'J',         'K',         'L',         ':',
    /* 0x28 */ '"',         '~',         KEY_LSHIFT,  '|',
    /* 0x2C */ 'Z',         'X',         'C',         'V',
    /* 0x30 */ 'B',         'N',         'M',         '<',
    /* 0x34 */ '>',         '?',         KEY_RSHIFT,  '*',
    /* 0x38 */ KEY_LALT,   ' ',         KEY_CAPS,    KEY_F1,
    /* 0x3C */ KEY_F2,     KEY_F3,      KEY_F4,      KEY_F5,
    /* 0x40 */ KEY_F6,     KEY_F7,      KEY_F8,      KEY_F9,
    /* 0x44 */ KEY_F10,    KEY_NUMLOCK, KEY_SCROLLLOCK, '7',
    /* 0x48 */ '8',         '9',         '-',         '4',
    /* 0x4C */ '5',         '6',         '+',         '1',
    /* 0x50 */ '2',         '3',         '0',         '.',
    /* 0x54 */ KEY_NONE,   KEY_NONE,    KEY_NONE,    KEY_F11,
    /* 0x58 */ KEY_F12,
};

/* Extended (0xE0 prefixed) scancode mappings */
static const uint8_t keymap_ext[] = {
    /* 0x1C */ KEY_ENTER,  /* keypad Enter */
    /* 0x1D */ KEY_RCTRL,
    /* 0x35 */ '/',         /* keypad slash */
    /* 0x37 */ KEY_PRINTSCREEN,
    /* 0x38 */ KEY_RALT,
    /* 0x47 */ KEY_HOME,
    /* 0x48 */ KEY_UP,
    /* 0x49 */ KEY_PAGEUP,
    /* 0x4B */ KEY_LEFT,
    /* 0x4D */ KEY_RIGHT,
    /* 0x4F */ KEY_END,
    /* 0x50 */ KEY_DOWN,
    /* 0x51 */ KEY_PAGEDOWN,
    /* 0x52 */ KEY_INSERT,
    /* 0x53 */ KEY_DELETE,
    /* 0x5B */ KEY_LGUI,
    /* 0x5C */ KEY_RGUI,
};

#define EXT_SCAN_COUNT (sizeof(keymap_ext) / sizeof(keymap_ext[0]))

/*==============================================================================
 * Helper: Look up extended scancode
 *==============================================================================*/
static uint8_t lookup_extended(uint8_t scancode)
{
    for (uint8_t i = 0; i < (uint8_t)EXT_SCAN_COUNT; i += 2) {
        if (keymap_ext[i] == scancode) {
            return keymap_ext[(size_t)i + 1];
        }
    }
    return KEY_NONE;
}

/*==============================================================================
 * Helper: Is this scancode a modifier (make)?
 *==============================================================================*/
static uint8_t modifier_for_scancode(uint8_t scancode, uint8_t is_ext)
{
    if (is_ext) {
        if (scancode == 0x1D) return KBD_MOD_RCTRL;
        if (scancode == 0x38) return KBD_MOD_RALT;
        if (scancode == 0x5B) return KBD_MOD_LGUI;
        if (scancode == 0x5C) return KBD_MOD_RGUI;
    } else {
        if (scancode == 0x1D) return KBD_MOD_LCTRL;
        if (scancode == 0x2A) return KBD_MOD_LSHIFT;
        if (scancode == 0x36) return KBD_MOD_RSHIFT;
        if (scancode == 0x38) return KBD_MOD_LALT;
    }
    return 0;
}

/*==============================================================================
 * Helper: Add scancode to the circular buffer
 *==============================================================================*/
static void buffer_put(uint8_t value)
{
    uint16_t next = (uint16_t)((buf_head + 1) % KBD_BUF_SIZE);
    if (next != buf_tail) {
        kbd_buffer[buf_head] = value;
        buf_head = next;
    }
    /* else: buffer full, silently drop */
}

/*==============================================================================
 * Public: Keyboard IRQ Handler
 *==============================================================================*/
void keyboard_irq_handler(void)
{
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        /* Extended scancode prefix — next byte uses extended table */
        extended = 1;
        goto done;
    }

    uint8_t key     = KEY_NONE;
    uint8_t is_break = (scancode & 0x80) != 0;
    uint8_t scan7   = scancode & 0x7F;     /* strip break bit */

    if (extended) {
        /* Extended scancode (0xE0 prefix received previously) */
        if (is_break) {
            /* Extended key release — update modifiers */
            uint8_t mod = modifier_for_scancode(scan7, 1);
            modifiers &= ~mod;
        } else {
            uint8_t mod = modifier_for_scancode(scan7, 1);
            if (mod) {
                modifiers |= mod;
            } else {
                key = lookup_extended(scancode);
                if (key != KEY_NONE) {
                    buffer_put(key);
                }
            }
        }
        extended = 0;
        goto done;
    }

    /* Standard scancode (single-byte, no 0xE0 prefix) */
    if (is_break) {
        /* Key release — update modifiers */
        uint8_t mod = modifier_for_scancode(scan7, 0);
        modifiers &= ~mod;
        if (scan7 == 0x2A || scan7 == 0x36) {
            /* Shift released — no need for extra action */
        }
        goto done;
    }

    /* Key press */
    {
        uint8_t mod = modifier_for_scancode(scancode, 0);
        if (mod) {
            modifiers |= mod;
            if (scancode == 0x1D) buffer_put(KEY_LCTRL);
            if (scancode == 0x2A) buffer_put(KEY_LSHIFT);
            if (scancode == 0x36) buffer_put(KEY_RSHIFT);
            if (scancode == 0x38) buffer_put(KEY_LALT);
            goto done;
        }
    }

    /* Look up ASCII value from keymap */
    if (scancode < MAX_SCAN) {
        uint8_t shifted = (modifiers & (KBD_MOD_LSHIFT | KBD_MOD_RSHIFT)) != 0;
        /* Caps lock toggles letters only */
        if (shifted) {
            key = keymap_us_shift[scancode];
        } else {
            key = keymap_us[scancode];
        }
        if (key != KEY_NONE) {
            buffer_put(key);
        }
    }

done:
    /* Must read port 0x64 to acknowledge — but reading 0x60 already does
       this for the PS/2 controller in most implementations.  Send EOI
       to the PIC is done by the caller (irq.c). */
    (void)0;
}

/*==============================================================================
 * Public API
 *==============================================================================*/

void keyboard_init(void)
{
    modifiers = 0;
    extended = 0;
    buf_head = 0;
    buf_tail = 0;

    /* Flush any stale data from the PS/2 controller */
    for (int i = 0; i < 4; i++) {
        (void)inb(0x60);
    }

    /* Enable the keyboard port (PS/2 controller command 0xAE) */
    outb(0x64, 0xAE);
}

uint8_t keyboard_getchar(void)
{
    if (buf_head == buf_tail) {
        return 0;
    }
    uint8_t c = kbd_buffer[buf_tail];
    buf_tail = (uint16_t)((buf_tail + 1) % KBD_BUF_SIZE);
    return c;
}

uint8_t keyboard_has_data(void)
{
    return (uint8_t)(buf_head != buf_tail);
}

uint8_t keyboard_get_modifiers(void)
{
    return modifiers;
}
