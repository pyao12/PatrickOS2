#include <common.h>
#include <graphics/basic.h>

static graphics_state_t g_graphics;

void graphics_init(const mb_info_t *mb_info) {
    if (mb_info == 0 || (mb_info->flags & (1u << 12)) == 0 ||
        mb_info->framebuffer_type != 1 || mb_info->framebuffer_width == 0 ||
        mb_info->framebuffer_height == 0 || 
        (mb_info->framebuffer_bpp != 24 && mb_info->framebuffer_bpp != 32)) { // 确认满足init条件
            halt(); // 以后重要步骤出错 halt(); return; 会很常见
            return;
        }
        
    // 逐个赋值
    g_graphics.framebuffer = (volatile ui8 *) (uip) mb_info->framebuffer_addr;
    g_graphics.width = mb_info->framebuffer_width;
    g_graphics.height = mb_info->framebuffer_height;
    g_graphics.bytes_per_pixel = (ui8) (mb_info->framebuffer_bpp / 8);
    g_graphics.bits_per_pixel = mb_info->framebuffer_bpp;
    g_graphics.initialized = true;
}