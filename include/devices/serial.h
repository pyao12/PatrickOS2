#pragma once

#include <common.h>

// functions
void serial_init();
void serial_write_char(char c);
void serial_write_str(const char *str);
void serial_write_uint(ui64 value);