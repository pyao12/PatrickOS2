#include <graphics/basic.h>
#include <graphics/font.h>
#include <graphics/layer.h>

constexpr i32  console_x      = 120;
constexpr i32  console_y      = 120;
constexpr ui32 console_width  = 480;
constexpr ui32 console_height = 320;
constexpr int  max_line       = console_height / 16;
constexpr int  line_max_char  = console_width / 8;

static ui16     current_line = 0, current_char = 0;
static layer_t *console_layer;

static bool ensure_console_layer() {
    if (console_layer != 0)
        return true;
    console_layer = layer_create(console_x, console_y, console_width, console_height, 100);
    return console_layer != 0;
}

void clear_console() {
    if (ensure_console_layer())
        layer_fill(console_layer, COLOR_BLACK);
    current_line = 0;
    current_char = 0;
}

static void prepare_cursor() {
    if (current_char >= line_max_char) {
        current_char = 0;
        current_line++;
    }
    if (current_line >= max_line) {
        clear_console();
        current_line = 0;
        current_char = 0;
    }
}

void write_console(const char *str, ui32 color) {
    if (str == 0 || !ensure_console_layer())
        return;
    for (const char *cursor = str; *cursor != 0; cursor++) {
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
            print_char(console_layer, *cursor, current_char * 8, current_line * 16, color);
            current_char++;
        }
    }
}

void erase_console_char() {
    if (current_char == 0)
        return;
    current_char--;
    print_char(console_layer, ' ', current_char * 8, current_line * 16, COLOR_WHITE);
}
