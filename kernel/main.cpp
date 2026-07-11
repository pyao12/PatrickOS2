#include <common.h>
#include <graphics/basic.h>
#include <graphics/font.h>
#include <console-entry.h>
#include <scheduler.h>

#define screen_width  1280
#define screen_height 768

void taska(void *arg) {
    (void) arg;
    for (int i = 1; i <= 100000; i++) {
        print_char((char) ('0' + i % 10), 20, 20, 0x00ffffff);
        scheduler_yield();
    }
}

void taskb(void *arg) {
    (void) arg;
    for (int i = 1; i <= 100000; i++) {
        print_char((char) ('0' + i % 10), 20, 50, 0x00ffffff);
        scheduler_yield();
    }
}

extern "C" void kernel_main(ui32 mb_info_addr) { 
    // 函数设置为 extern "C" 是因为 boot.s 使用 clang 编译，需要提供C兼容接口调用
    mb_info_t *mb_info = (mb_info_t *) (uip) mb_info_addr;
    graphics_init(mb_info);

    scheduler_init();
    if (scheduler_create_task(console_main, 0) < 0) halt();
    if (scheduler_create_task(taska, 0) < 0) halt();
    if (scheduler_create_task(taskb, 0) < 0) halt();

    scheduler_run();

    // console_main();
    // 为了测试多任务我们先把console给关掉
    
    halt(); // 因为现在系统没有更多任务了，所以halt()，比while (true)更省电
}