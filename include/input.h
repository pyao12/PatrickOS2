#pragma once

#include <common.h>

constexpr ui32 input_buffer_size = 256;

struct input_buffer_t {
    char data[input_buffer_size];
    ui32 head;
    ui32 tail;
};

void input_buffer_init(input_buffer_t *buf);
bool input_buffer_push(input_buffer_t *buf, char c);
bool input_buffer_pop(input_buffer_t *buf, char *c);
bool input_buffer_empty(input_buffer_t *buf);

extern input_buffer_t global_input_buffer;
