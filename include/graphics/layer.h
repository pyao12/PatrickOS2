#pragma once

#include <common.h>

constexpr int  layer_max_count   = 16;
constexpr ui32 layer_transparent = 0xff000000;

struct layer_t {
    ui32 *pixels;
    i32   x;
    i32   y;
    ui32  width;
    ui32  height;
    i32   z_index;
    ui64  page_count;
    bool  visible;
    bool  active;
    bool  raise_on_click;
};

void     layer_manager_init();
layer_t *layer_create(i32 x, i32 y, ui32 width, ui32 height, i32 z_index, bool raise_on_click = true);
void     layer_destroy(layer_t *layer);
void     layer_set_position(layer_t *layer, i32 x, i32 y);
void     layer_set_visible(layer_t *layer, bool visible);
void     layer_set_z_index(layer_t *layer, i32 z_index);
void     layer_clear(layer_t *layer);
void     layer_fill(layer_t *layer, ui32 color);
void     layer_draw_pixel(layer_t *layer, i32 x, i32 y, ui32 color);
void     layer_fill_rect(layer_t *layer, i32 x, i32 y, ui32 width, ui32 height, ui32 color);
void     layer_compose();
void     layer_manager_main(void *);
