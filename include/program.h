#pragma once

#include <common.h>

constexpr ui8 program_directory_attribute_directory = 0x10;

struct program_directory_entry_t {
    char name[13];
    ui8  attributes;
};

struct program_api_t {
    void (*write_console)(const char *text, ui32 color);
    void (*yield)();
    const char *argument;
    const char *cwd;
    i64 (*read_file)(const char *path, ui32 offset, ui8 *buffer, ui32 size);
    i64 (*list_directory)(const char *path, program_directory_entry_t *entries,
                          ui32 capacity);
    i64 (*create_file)(const char *path);
    i64 (*create_directory)(const char *path);
    i64 (*write_file)(const char *path, ui32 offset, const ui8 *buffer,
                      ui32 size);
    i64 (*rename_path)(const char *path, const char *new_path);
    i64 (*remove_path)(const char *path);
};

bool program_run(const char *path, const char *argument = "",
                 const char *cwd = "/");
