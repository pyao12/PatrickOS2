#pragma once

#include <common.h>

constexpr ui8 program_directory_attribute_directory = 0x10;

struct program_directory_entry_t {
    char name[13];
    ui8  attributes;
};

using program_layer_t = ui32;

constexpr program_layer_t program_layer_invalid     = 0;
constexpr ui32            program_layer_transparent = 0xff000000;

struct program_layer_create_t {
    i32  x;
    i32  y;
    ui32 width;
    ui32 height;
    i32  z_index;
    bool raise_on_click;
};

struct program_layer_rect_t {
    program_layer_t layer;
    i32             x;
    i32             y;
    ui32            width;
    ui32            height;
    ui32            color;
};

struct program_api_t {
    void (*write_console)(const char *text, ui32 color);
    void (*yield)();
    const char *argument;
    const char *cwd;
    i64 (*read_file)(const char *path, ui32 offset, ui8 *buffer, ui32 size);
    i64 (*list_directory)(const char *path, program_directory_entry_t *entries, ui32 capacity);
    i64 (*create_file)(const char *path);
    i64 (*create_directory)(const char *path);
    i64 (*write_file)(const char *path, ui32 offset, const ui8 *buffer, ui32 size);
    i64 (*rename_path)(const char *path, const char *new_path);
    i64 (*remove_path)(const char *path);
    bool (*read_input)(char *character);
    void (*clear_console)();
    void (*erase_console_char)();
    bool (*run_program)(const char *path, const char *argument, const char *cwd);
    program_layer_t (*layer_create)(const program_layer_create_t *properties);
    void (*layer_destroy)(program_layer_t layer);
    void (*layer_set_position)(program_layer_t layer, i32 x, i32 y);
    void (*layer_set_visible)(program_layer_t layer, bool visible);
    void (*layer_set_z_index)(program_layer_t layer, i32 z_index);
    void (*layer_clear)(program_layer_t layer);
    void (*layer_fill)(program_layer_t layer, ui32 color);
    void (*layer_draw_pixel)(program_layer_t layer, i32 x, i32 y, ui32 color);
    void (*layer_fill_rect)(const program_layer_rect_t *rect);
};

bool program_run(const char *path, const char *argument = "", const char *cwd = "/");
