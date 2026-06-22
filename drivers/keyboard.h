#ifndef ARES_KEYBOARD_H
#define ARES_KEYBOARD_H

#include <stdint.h>

/* Keyboard buffer size */
#define KBD_BUF_SIZE 256

/* Modifier flags */
#define KBD_MOD_LCTRL  0x01
#define KBD_MOD_LSHIFT 0x02
#define KBD_MOD_LALT   0x04
#define KBD_MOD_LGUI   0x08
#define KBD_MOD_RCTRL  0x10
#define KBD_MOD_RSHIFT 0x20
#define KBD_MOD_RALT   0x40
#define KBD_MOD_RGUI   0x80

/* Special key codes (returned when no ASCII equivalent) */
#define KEY_NONE       0x00
#define KEY_ESC        0x1B
#define KEY_BACKSPACE  0x08
#define KEY_TAB        0x09
#define KEY_ENTER      0x0D
#define KEY_UP         0x80
#define KEY_DOWN       0x81
#define KEY_LEFT       0x82
#define KEY_RIGHT      0x83
#define KEY_HOME       0x84
#define KEY_END        0x85
#define KEY_PAGEUP     0x86
#define KEY_PAGEDOWN   0x87
#define KEY_DELETE     0x88
#define KEY_INSERT     0x89
#define KEY_F1         0x8A
#define KEY_F2         0x8B
#define KEY_F3         0x8C
#define KEY_F4         0x8D
#define KEY_F5         0x8E
#define KEY_F6         0x8F
#define KEY_F7         0x90
#define KEY_F8         0x91
#define KEY_F9         0x92
#define KEY_F10        0x93
#define KEY_F11        0x94
#define KEY_F12        0x95
#define KEY_CAPS       0x96
#define KEY_NUMLOCK    0x97
#define KEY_SCROLLLOCK 0x98
#define KEY_LCTRL      0x99
#define KEY_LSHIFT     0x9A
#define KEY_LALT       0x9B
#define KEY_RCTRL      0x9C
#define KEY_RSHIFT     0x9D
#define KEY_RALT       0x9E
#define KEY_LGUI       0x9F
#define KEY_RGUI       0xA0
#define KEY_PRINTSCREEN 0xA1

/* Initialize the keyboard driver */
void keyboard_init(void);

/* IRQ handler (called from irq.c) */
void keyboard_irq_handler(void);

/* Read a key from the buffer. Returns 0 if buffer is empty. */
uint8_t keyboard_getchar(void);

/* Check if a key is available */
uint8_t keyboard_has_data(void);

/* Read modifiers */
uint8_t keyboard_get_modifiers(void);

#endif /* ARES_KEYBOARD_H */
