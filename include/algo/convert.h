#pragma once

#include <common.h>

inline char *int2str(ui64 value, char buffer[21]) {
    int index     = 20;
    buffer[index] = '\0';

    do {
        buffer[--index] = '0' + (value % 10);
        value /= 10;
    } while (value != 0);

    return &buffer[index];
}
