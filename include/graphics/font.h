#pragma once

// 目前仅实现 ASC16 格式的点阵字体，truetype 等之后再说

#include <common.h>
#include "__fontdat.h"

void print_char(char c, ui32 posx, ui32 posy, ui32 color);
void print_str(const char* str, ui32 posx, ui32 posy, ui32 color);
