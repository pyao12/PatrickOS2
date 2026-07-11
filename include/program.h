#pragma once

#include <common.h>

struct program_api_t {
    void (*write_console)(const char *text, ui32 color);
    void (*yield)();
};

bool program_run(const char *path);
