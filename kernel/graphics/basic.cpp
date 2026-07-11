#include <common.h>
#include <graphics/basic.h>

static graphics_state_t g_graphics;

void graphics_init(const mb_info_t *mb_info) {
    if (mb_info == 0 || (mb_info->flags & (1u << 12)) == 0 || mb_info->framebuffer_type != 1 || 
        mb_info->framebuffer_width == 0 || mb_info->framebuffer_height == 0 || 
        (mb_info->framebuffer_bpp != 24 && mb_info->framebuffer_bpp != 32)) { // 确认满足init条件
            halt(); // 以后重要步骤出错 halt(); return; 会很常见
            return;
        }
        
    // 逐个赋值
    g_graphics.framebuffer     = (volatile ui8 *) (uip) mb_info->framebuffer_addr;
    g_graphics.width           = mb_info->framebuffer_width;
    g_graphics.height          = mb_info->framebuffer_height;
    g_graphics.pitch           = mb_info->framebuffer_pitch;
    g_graphics.bytes_per_pixel = (ui8) (mb_info->framebuffer_bpp / 8);
    g_graphics.bits_per_pixel  = mb_info->framebuffer_bpp;
    g_graphics.initialized     = true;
}

void draw_pixel(ui32 posx, ui32 posy, ui32 color) {
    // color设置为ui32，是因为ui32的hex可以表示RGB，例如
    // RGB #114514 -> ui32 0x00114514
    volatile ui8 *pixel;
    ui32 x = posx % g_graphics.width, y = posy % g_graphics.height;

    pixel = g_graphics.framebuffer + (y * g_graphics.pitch) + (x * g_graphics.bytes_per_pixel);
    if (g_graphics.bits_per_pixel == 32) {
        *(volatile ui32 *) pixel = color;
    } else {
        pixel[0] = (ui8) ( color        & 0xff);
        pixel[1] = (ui8) ((color >> 8)  & 0xff);
        pixel[2] = (ui8) ((color >> 16) & 0xff);
    }
}