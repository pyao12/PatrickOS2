#include <input.h>

input_buffer_t global_input_buffer;

void input_buffer_init(input_buffer_t *buf) {
    buf->head = 0;
    buf->tail = 0;
}

bool input_buffer_push(input_buffer_t *buf, char c) {
    ui32 next = (buf->head + 1) % input_buffer_size;
    if (next == buf->tail)
        return false;
    buf->data[buf->head] = c;
    buf->head            = next;
    return true;
}

bool input_buffer_pop(input_buffer_t *buf, char *c) {
    if (buf->head == buf->tail)
        return false;
    *c        = buf->data[buf->tail];
    buf->tail = (buf->tail + 1) % input_buffer_size;
    return true;
}

bool input_buffer_empty(input_buffer_t *buf) { return buf->head == buf->tail; }
