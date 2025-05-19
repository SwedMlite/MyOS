#include "system.h"

/* KBDUS means US Keyboard Layout. This is a scancode table
*  used to layout a standard US keyboard. I have left some
*  comments in to give you an idea of what key is what, even
*  though I set it's array index to 0. You can change that to
*  whatever you want using a macro, if you wish! */
unsigned char kbdus[128] =
{
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '-', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',		/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n',			/* 49 */
  'm', ',', '.', '/',   0,					/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};

static int caps_lock_state = 0;

/* Handles the keyboard interrupt */
void keyboard_handler(struct regs *r)
{
    unsigned char scancode;
    unsigned char c;

    /* Read from the keyboard's data buffer */
    scancode = inportb(0x60);

    /* If the top bit of the byte we read from the keyboard is
    * set, that means that a key has just been released */
    if (scancode & 0x80)
    {
        // Key release - ignore for simple shell
    }
    else
    {
         if (scancode == 58) {
            caps_lock_state = !caps_lock_state; // switch state Caps Lock
            return;
        }
        // Key press
        c = kbdus[scancode]; // transform scancode into ASCII

         if (caps_lock_state && (c >= 'a' && c <= 'z')) {
            // transform in capital letter
            // difference bettween capital and small is 32
            c = c - 32;
        }

        if (c == '\b') // Backspace
        {
            if (command_buffer_pos > 0) {
                command_buffer_pos--;
                putch('\b'); // Move cursor backward
                putch(' ');  // Erase symbol
                putch('\b'); // Again move cursor backward
            }
        }
        else if (c == '\n') // Enter
        {
            // Add symbol new string in buffer and print him.
            if (command_buffer_pos < COMMAND_BUFFER_SIZE - 1) {
                command_buffer[command_buffer_pos++] = c;
                putch(c);
            }
        }
        else if (c >= ' ') // ignore tabs and other not important symbols
        {
            // Add symbol in buffer and print him.
            if (command_buffer_pos < COMMAND_BUFFER_SIZE - 1) {
                command_buffer[command_buffer_pos++] = c;
                putch(c);
            }
        }
        // ignore other symbols (Ctrl, Alt, F-keys, и т.д.)
    }
}

/* Installs the keyboard handler into IRQ1 */
void keyboard_install()
{
    irq_install_handler(1, keyboard_handler);
}
