#pragma once

#include <common.h>

typedef struct ps2_mouse_state {
    i64  x;
    i64  y;
    bool left_button;
    bool right_button;
    bool middle_button;
    ui64 packet_count;
} ps2_mouse_state_t;

bool              ps2mouse_init();
ps2_mouse_state_t ps2mouse_get_state();
void              ps2mouse_main(void *arg);
