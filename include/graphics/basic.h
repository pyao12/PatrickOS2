#pragma once

#include <common.h>

// 类型定义
typedef struct mb_info {
    ui32 flags;
    ui32 mem_lower;
    ui32 mem_upper;
    ui32 boot_device;
    ui32 cmdline;
    ui32 mods_count;
    ui32 mods_addr;
    ui8 syms[16];
    ui32 mmap_length;
    ui32 mmap_addr;
    ui32 drives_length;
    ui32 drives_addr;
    ui32 configable;
    ui32 boot_loader_name;
    ui32 apmable;
    ui32 vbe_control_info;
    ui32 vbe_mode_info;
    ui16 vbe_mode;
    ui16 vbe_interface_seg;
    ui16 vbe_interface_off;
    ui16 vbe_interface_len;
    ui64 framebuffer_addr;
    ui32 framebuffer_pitch;
    ui32 framebuffer_width;
    ui32 framebuffer_height;
    ui8 framebuffer_bpp;
    ui8 framebuffer_type;
    ui8 color_info[6];
} __attribute__ ((packed)) mb_info_t;

typedef struct graphics_state {
    volatile ui8 *framebuffer;
    ui32 width;
    ui32 height;
    ui32 pitch;
    ui8 bytes_per_pixel;
    ui8 bits_per_pixel;
    bool initialized;
} graphics_state_t;

// 函数声明
void graphics_init(const mb_info_t *mb_info);