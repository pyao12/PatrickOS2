#include <common.h>
#include <graphics/basic.h>

#define screen_width  1280
#define screen_height 768

extern "C" void kernel_main(ui32 mb_info_addr) { 
    // 函数设置为 extern "C" 是因为 boot.s 使用 clang 编译，需要提供C兼容接口调用
    mb_info_t *mb_info = (mb_info_t *) (uip) mb_info_addr;
    graphics_init(mb_info);
    halt(); // 因为现在系统没有更多任务了，所以halt()，比while (true)更省电
}