#include <devices/ps2mouse.h>
#include <graphics/basic.h>
#include <graphics/colors.h>
#include <graphics/layer.h>
#include <memory.h>
#include <scheduler.h>

static layer_t layers[layer_max_count];

static bool layer_is_valid(const layer_t *layer) {
    return layer >= layers && layer < layers + layer_max_count && layer->active;
}

void layer_manager_init() {
    for (int index = 0; index < layer_max_count; index++)
        layers[index].active = false;
}

layer_t *layer_create(i32 x, i32 y, ui32 width, ui32 height, i32 z_index,
                      bool raise_on_click) {
    if (width == 0 || height == 0)
        return 0;

    layer_t *layer = 0;
    for (int index = 0; index < layer_max_count; index++) {
        if (!layers[index].active) {
            layer = &layers[index];
            break;
        }
    }
    if (layer == 0)
        return 0;

    ui64  byte_count = static_cast<ui64>(width) * height * sizeof(ui32);
    ui64  page_count = (byte_count + 4095) / 4096;
    ui32 *pixels     = static_cast<ui32 *>(memory_alloc_pages(page_count));
    if (pixels == 0)
        return 0;

    layer->pixels         = pixels;
    layer->x              = x;
    layer->y              = y;
    layer->width          = width;
    layer->height         = height;
    layer->z_index        = z_index;
    layer->page_count     = page_count;
    layer->visible        = true;
    layer->active         = true;
    layer->raise_on_click = raise_on_click;
    layer_clear(layer);
    return layer;
}

void layer_destroy(layer_t *layer) {
    if (!layer_is_valid(layer))
        return;
    memory_free_pages(layer->pixels, layer->page_count);
    layer->active = false;
}

void layer_set_position(layer_t *layer, i32 x, i32 y) {
    if (!layer_is_valid(layer))
        return;
    layer->x = x;
    layer->y = y;
}

void layer_set_visible(layer_t *layer, bool visible) {
    if (layer_is_valid(layer))
        layer->visible = visible;
}

void layer_set_z_index(layer_t *layer, i32 z_index) {
    if (layer_is_valid(layer))
        layer->z_index = z_index;
}

void layer_clear(layer_t *layer) {
    if (!layer_is_valid(layer))
        return;
    layer_fill(layer, layer_transparent);
}

void layer_fill(layer_t *layer, ui32 color) {
    if (!layer_is_valid(layer))
        return;
    ui64 pixel_count = static_cast<ui64>(layer->width) * layer->height;
    for (ui64 index = 0; index < pixel_count; index++)
        layer->pixels[index] = color;
}

void layer_draw_pixel(layer_t *layer, i32 x, i32 y, ui32 color) {
    if (!layer_is_valid(layer) || x < 0 || y < 0 ||
        x >= static_cast<i32>(layer->width) ||
        y >= static_cast<i32>(layer->height))
        return;
    layer->pixels[static_cast<ui64>(y) * layer->width + x] = color;
}

void layer_fill_rect(layer_t *layer, i32 x, i32 y, ui32 width, ui32 height,
                     ui32 color) {
    if (!layer_is_valid(layer))
        return;
    for (ui32 offset_y = 0; offset_y < height; offset_y++) {
        for (ui32 offset_x = 0; offset_x < width; offset_x++)
            layer_draw_pixel(layer, x + offset_x, y + offset_y, color);
    }
}

void layer_compose() {
    ui32 screen_width  = graphics_width();
    ui32 screen_height = graphics_height();
    for (ui32 y = 0; y < screen_height; y++) {
        for (ui32 x = 0; x < screen_width; x++) {
            ui32 color = COLOR_BLACK;
            i32  top_z = -2147483647 - 1;
            for (int index = 0; index < layer_max_count; index++) {
                layer_t &layer   = layers[index];
                i32      local_x = static_cast<i32>(x) - layer.x;
                i32      local_y = static_cast<i32>(y) - layer.y;
                if (!layer.active || !layer.visible || layer.z_index < top_z ||
                    local_x < 0 || local_y < 0 ||
                    local_x >= static_cast<i32>(layer.width) ||
                    local_y >= static_cast<i32>(layer.height))
                    continue;
                ui32 pixel =
                    layer.pixels[static_cast<ui64>(local_y) * layer.width +
                                 local_x];
                if (pixel != layer_transparent) {
                    color = pixel;
                    top_z = layer.z_index;
                }
            }
            draw_pixel(x, y, color);
        }
        scheduler_yield();
    }
}

static void raise_clicked_layer(i32 x, i32 y) {
    layer_t *clicked_layer = 0;
    layer_t *highest_layer = 0;
    i32      clicked_z     = -2147483647 - 1;
    i32      highest_z     = -2147483647 - 1;

    for (int index = 0; index < layer_max_count; index++) {
        layer_t &layer = layers[index];
        if (!layer.active || !layer.visible)
            continue;

        if (layer.z_index > highest_z) {
            highest_z     = layer.z_index;
            highest_layer = &layer;
        }

        i32 local_x = x - layer.x;
        i32 local_y = y - layer.y;
        if (local_x < 0 || local_y < 0 ||
            local_x >= static_cast<i32>(layer.width) ||
            local_y >= static_cast<i32>(layer.height))
            continue;

        ui32 pixel =
            layer.pixels[static_cast<ui64>(local_y) * layer.width + local_x];
        if (pixel != layer_transparent && layer.z_index >= clicked_z) {
            clicked_layer = &layer;
            clicked_z     = layer.z_index;
        }
    }

    if (clicked_layer != 0 && clicked_layer->raise_on_click &&
        highest_layer != 0 && clicked_layer != highest_layer) {
        highest_layer->z_index = clicked_layer->z_index;
        clicked_layer->z_index = highest_z;
    }
}

static void create_examples() {
    layer_t *desktop =
        layer_create(0, 0, graphics_width(), graphics_height(), 0, false);
    layer_t *panel   = layer_create(0, 0, graphics_width(), 28, 10);
    layer_t *window1 = layer_create(70, 70, 300, 190, 20);
    layer_t *window2 = layer_create(250, 170, 320, 210, 30);

    if (desktop != 0)
        layer_fill(desktop, 0x00243842);
    if (panel != 0) {
        layer_fill(panel, 0x00141b20);
        layer_fill_rect(panel, 10, 7, 52, 14, COLOR_GREEN);
    }
    if (window1 != 0) {
        layer_fill(window1, 0x00e7edf0);
        layer_fill_rect(window1, 0, 0, window1->width, 24, 0x001e88a8);
        layer_fill_rect(window1, 18, 48, 120, 18, COLOR_WHITE);
        layer_fill_rect(window1, 18, 82, 210, 12, COLOR_GRAY);
    }
    if (window2 != 0) {
        layer_fill(window2, 0x00f4f0df);
        layer_fill_rect(window2, 0, 0, window2->width, 24, 0x00d0523a);
        layer_fill_rect(window2, 22, 52, 80, 80, COLOR_ORANGE);
        layer_fill_rect(window2, 122, 52, 170, 18, COLOR_BLACK);
        layer_fill_rect(window2, 122, 86, 130, 12, COLOR_GRAY);
    }
}

void layer_manager_main(void *) {
    create_examples();
    bool was_left_button_down = false;
    while (true) {
        ps2_mouse_state_t mouse = ps2mouse_get_state();
        if (mouse.left_button && !was_left_button_down) {
            i32 cursor_x;
            i32 cursor_y;
            graphics_get_cursor_position(&cursor_x, &cursor_y);
            raise_clicked_layer(cursor_x, cursor_y);
        }
        was_left_button_down = mouse.left_button;
        layer_compose();
        scheduler_yield();
    }
}
