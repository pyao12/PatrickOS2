#pragma once

#include <common.h>

#define COM1_PORT 0x03f8

static void outb(ui16 port, ui8 value) { asm("outb %0, %1" : : "a"(value), "Nd"(port)); }

static ui8 inb(ui16 port) {
    ui8 result;
    asm("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}