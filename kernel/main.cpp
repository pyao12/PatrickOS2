#include <common.h>
#include <graphics/basic.h>
#include <console.h>
#include <fs/fat32.h>
#include <memory.h>
#include <scheduler.h>
#include <devices/keyboard.h>
#include <input.h>

extern "C" void kernel_main(ui32 mb_info_addr) {
    mb_info_t *mb_info = (mb_info_t *) (uip) mb_info_addr;
    graphics_init(mb_info);
    memory_init(mb_info);

    if (!fat32_mount_primary_ata()) halt();

    input_buffer_init(&global_input_buffer);
    scheduler_init();
    if (scheduler_create_task(console_main, 0) < 0) halt();
    if (scheduler_create_task(keyboard_main, 0) < 0) halt();

    scheduler_run();
    halt();
}
