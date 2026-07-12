#include <algo/convert.h>
#include <devices/io.h>
#include <devices/serial.h>

void serial_init() {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xc7);
    outb(COM1_PORT + 4, 0x0b);
}

void serial_write_char(char c) {
    while ((inb(COM1_PORT + 5) & 0x20u) == 0)
        ; // 防串
    outb(COM1_PORT, (ui8)c);
}

void serial_write_str(const char *str) {
    if (str == 0)
        return;
    while (*str != 0) {
        if (*str == '\n')
            serial_write_char('\r');
        serial_write_char(*str);
        str++;
    }
}

void serial_write_uint(ui64 value) {
    char buffer[20];
    serial_write_str(int2str(value, buffer));
}