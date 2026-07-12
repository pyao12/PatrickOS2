#include <common.h>
#include <graphics/basic.h>

static graphics_state_t g_graphics;

static constexpr ui32 cursor_width                                  = 12;
static constexpr ui32 cursor_height                                 = 16;
static const char     cursor_shape[cursor_height][cursor_width + 1] = {
    "B...........", "BB..........", "BWB.........", "BWWB........",
    "BWWWB.......", "BWWWWB......", "BWWWWWB.....", "BWWWWWWB....",
    "BWWBBBBBB...", "BWWBWB......", "BWB.BWB.....", "BB..BWB.....",
    "B....BWB....", ".....BWB....", ".....BB.....", "............",
};

static struct {
    i32  x;
    i32  y;
    ui32 background[cursor_height][cursor_width];
    bool visible;
} cursor;

static ui32 read_pixel(ui32 x, ui32 y) {
    volatile ui8 *pixel = g_graphics.framebuffer + y * g_graphics.pitch +
                          x * g_graphics.bytes_per_pixel;
    if (g_graphics.bits_per_pixel == 32)
        return *(volatile ui32 *)pixel;
    return pixel[0] | (static_cast<ui32>(pixel[1]) << 8) |
           (static_cast<ui32>(pixel[2]) << 16);
}

static void write_pixel(ui32 x, ui32 y, ui32 color) {
    volatile ui8 *pixel = g_graphics.framebuffer + y * g_graphics.pitch +
                          x * g_graphics.bytes_per_pixel;
    if (g_graphics.bits_per_pixel == 32) {
        *(volatile ui32 *)pixel = color;
    } else {
        pixel[0] = static_cast<ui8>(color);
        pixel[1] = static_cast<ui8>(color >> 8);
        pixel[2] = static_cast<ui8>(color >> 16);
    }
}

void graphics_init(const mb_info_t *mb_info) {
    if (mb_info == 0 || (mb_info->flags & (1u << 12)) == 0 ||
        mb_info->framebuffer_type != 1 || mb_info->framebuffer_width == 0 ||
        mb_info->framebuffer_height == 0 ||
        (mb_info->framebuffer_bpp != 24 &&
         mb_info->framebuffer_bpp != 32)) { // 确认满足init条件
        halt(); // 以后重要步骤出错 halt(); return; 会很常见
        return;
    }

    // 逐个赋值
    g_graphics.framebuffer     = (volatile ui8 *)(uip)mb_info->framebuffer_addr;
    g_graphics.width           = mb_info->framebuffer_width;
    g_graphics.height          = mb_info->framebuffer_height;
    g_graphics.pitch           = mb_info->framebuffer_pitch;
    g_graphics.bytes_per_pixel = (ui8)(mb_info->framebuffer_bpp / 8);
    g_graphics.bits_per_pixel  = mb_info->framebuffer_bpp;
    g_graphics.initialized     = true;
}

void draw_pixel(ui32 posx, ui32 posy, ui32 color) {
    // color设置为ui32，是因为ui32的hex可以表示RGB，例如
    // RGB #114514 -> ui32 0x00114514
    ui32 x = posx % g_graphics.width, y = posy % g_graphics.height;

    if (cursor.visible && x >= static_cast<ui32>(cursor.x) &&
        y >= static_cast<ui32>(cursor.y)) {
        ui32 cursor_x = x - cursor.x;
        ui32 cursor_y = y - cursor.y;
        if (cursor_x < cursor_width && cursor_y < cursor_height &&
            cursor_shape[cursor_y][cursor_x] != '.') {
            cursor.background[cursor_y][cursor_x] = color;
            return;
        }
    }
    write_pixel(x, y, color);
}

void graphics_move_cursor(i32 delta_x, i32 delta_y) {
    if (cursor.visible) {
        for (ui32 y = 0; y < cursor_height; y++) {
            for (ui32 x = 0; x < cursor_width; x++) {
                if (cursor_shape[y][x] != '.')
                    write_pixel(cursor.x + x, cursor.y + y,
                                cursor.background[y][x]);
            }
        }
    } else {
        cursor.x       = static_cast<i32>(g_graphics.width / 2);
        cursor.y       = static_cast<i32>(g_graphics.height / 2);
        cursor.visible = true;
    }

    cursor.x += delta_x;
    cursor.y += delta_y;
    if (cursor.x < 0)
        cursor.x = 0;
    if (cursor.y < 0)
        cursor.y = 0;
    if (cursor.x > static_cast<i32>(g_graphics.width - cursor_width))
        cursor.x = g_graphics.width - cursor_width;
    if (cursor.y > static_cast<i32>(g_graphics.height - cursor_height))
        cursor.y = g_graphics.height - cursor_height;

    for (ui32 y = 0; y < cursor_height; y++) {
        for (ui32 x = 0; x < cursor_width; x++) {
            char pixel = cursor_shape[y][x];
            if (pixel == '.')
                continue;
            cursor.background[y][x] = read_pixel(cursor.x + x, cursor.y + y);
            write_pixel(cursor.x + x, cursor.y + y,
                        pixel == 'B' ? 0x000000 : 0xFFFFFF);
        }
    }
}
