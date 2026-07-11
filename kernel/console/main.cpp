#include <graphics/font.h>
#include <graphics/basic.h>
#include <memory.h>
#include <scheduler.h>

#include <algo/convert.h>

constexpr int max_line      = 48;  // 768 / 16
constexpr int line_max_char = 160; // 1280 / 8

ui16 current_line = 0, current_char = 0;

void clear_screen() {
    for (int x = 0; x <= 1280; x++) {
        for (int y = 0; y < 768; y++) {
            draw_pixel(x, y, 0);
        }
    }
}

static void prepare_cursor() {
    if (current_char >= line_max_char) {
        current_char = 0;
        current_line++;
    }

    if (current_line >= max_line) {
        clear_screen();
        current_line = 0;
        current_char = 0;
    }
}

void write_console(const char* str, ui32 color = COLOR_WHITE) {
    if (str == 0) {
        return;
    }

    for (const char* cursor = str; *cursor != 0; cursor++) {
        if (*cursor == '\n') {
            current_char = 0;
            current_line++;
        } else if (*cursor == '\t') {
            for (int spaces = 0; spaces < 4; spaces++) {
                prepare_cursor();
                current_char++;
            }
        } else {
            prepare_cursor();
            print_char(*cursor, current_char * 8, current_line * 16, color);
            current_char++;
        }
    }
}

static void write_memory_size(const char *label, ui64 size) {
    char buffer[21];
    write_console(label);
    write_console(int2str(size / (1024 * 1024), buffer));
    write_console(" MiB\n");
}

void console_main(void *arg) {
    (void) arg;

    clear_screen();
    write_console("Welcome to PatrickOS 2 Console!\n");
    write_memory_size("Physical address range: ", memory_physical_size());
    write_memory_size("Usable physical memory: ", memory_usable_size());
    write_memory_size("Allocator free memory: ", memory_free_size());

    while (true) scheduler_yield();
}
