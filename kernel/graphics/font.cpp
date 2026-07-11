#include <graphics/font.h>
#include <graphics/basic.h>

void print_char(char c, ui32 posx, ui32 posy, ui32 color) {
    int index_begin = c * 16 + 1;

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            if (font_ASC16[row + index_begin] & (1 << col)) {
                draw_pixel(posx + 8 - col, posy + row, color);
            } else {
                draw_pixel(posx + 8 - col, posy + row, 0);
            }
        }
    }
}

void print_str(const char* str, ui32 posx, ui32 posy, ui32 color) {
    ui32 cursorx = posx, cursory = posy;
    for (const char *p = str; *p != 0; p++) {
        if (*p == '\n') {
            cursorx = posx, cursory += 16;
        } else if (*p == '\t') {
            cursorx += 32;
        } else {
            print_char(*p, cursorx, cursory, color);
            cursorx += 8;
        }
    }
}
