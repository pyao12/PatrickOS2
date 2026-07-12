#pragma once

#include <common.h>
#include <graphics/colors.h>

void write_console(const char *str, ui32 color = COLOR_WHITE);
void clear_console();
void erase_console_char();
