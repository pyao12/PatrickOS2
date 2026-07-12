#include <console.h>
#include <devices/keyboard.h>
#include <graphics/colors.h>
#include <input.h>
#include <scheduler.h>

static inline ui8 inb(ui16 port) {
    ui8 value;
    asm("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static const char sc1_normal[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9',  '0', '-', '=',  0,
    0,   'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',  '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,   '*',  0,   ' ', 0,    0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    '7', '8', '9',  '-',
    '4', '5', '6', '+', '1', '2', '3', '0', '.', 0,   0,    0,   0,
};

static const char sc1_shifted[128] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',  0,
    0,   'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|',  'Z',
    'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,    0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   '7', '8', '9',  '-',
    '4', '5', '6', '+', '1', '2', '3', '0', '.', 0,   0,   0,   0,
};

static bool is_letter(ui8 sc) {
    return (sc >= 0x10 && sc <= 0x19) || (sc >= 0x1E && sc <= 0x26) ||
           (sc >= 0x2C && sc <= 0x32);
}

void keyboard_main(void *arg) {
    (void)arg;

    bool shift    = false;
    bool capslock = false;

    while (true) {
        ui8 status = inb(0x64);
        if ((status & 0x21) == 0x01) {
            ui8 scancode = inb(0x60);
            if (scancode & 0x80) {
                ui8 brk = scancode & 0x7F;
                if (brk == 0x2A || brk == 0x36)
                    shift = false;
            } else {
                if (scancode == 0x2A || scancode == 0x36) {
                    shift = true;
                } else if (scancode == 0x3A) {
                    capslock = !capslock;
                } else if (scancode == 0x0E) {
                    input_buffer_push(&global_input_buffer, 0x08);
                } else {
                    char c;
                    if (is_letter(scancode)) {
                        bool upper = shift ^ capslock;
                        c          = upper ? sc1_shifted[scancode]
                                           : sc1_normal[scancode];
                    } else {
                        c = shift ? sc1_shifted[scancode]
                                  : sc1_normal[scancode];
                    }
                    if (c != 0) {
                        input_buffer_push(&global_input_buffer, c);
                    }
                }
            }
        }
        scheduler_yield();
    }
}
