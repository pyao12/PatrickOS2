#pragma once

#include <common.h>
#include <graphics/colors.h>

void console_main(void *arg);
void write_console(const char* str, ui32 color = COLOR_WHITE);
void clear_screen();
