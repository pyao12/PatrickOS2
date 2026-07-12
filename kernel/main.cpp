#include <common.h>
#include <console.h>
#include <devices/keyboard.h>
#include <devices/ps2mouse.h>
#include <devices/serial.h>
#include <fs/fat32.h>
#include <graphics/basic.h>
#include <graphics/layer.h>
#include <input.h>
#include <memory.h>
#include <program.h>
#include <scheduler.h>
#include <x86.h>

static void console_program_main(void *) {
    if (!program_run("/programs/console.elf"))
        halt();
}

extern "C" void kernel_main(ui32 mb_info_addr) {
    serial_init();
    serial_write_str("Welcome to PatrickOS 2! (You are on COM1 serial port)\n");

    mb_info_t *mb_info = (mb_info_t *)(uip)mb_info_addr;
    serial_write_str("Initializing graphics...\n");
    graphics_init(mb_info);

    serial_write_str("Initializing memory manager...\n");
    memory_init(mb_info);
    layer_manager_init();

    serial_write_str("Initializing x86...\n");
    x86_init();

    serial_write_str("Mounting Fat32 Primary Data partition...\n");
    if (!fat32_mount_primary_ata())
        halt();

    input_buffer_init(&global_input_buffer);

    serial_write_str("Initializing scheduler...\n");
    scheduler_init();

    if (scheduler_create_task(console_program_main, 0) < 0)
        halt();
    if (scheduler_create_task(layer_manager_main, 0) < 0)
        halt();
    if (scheduler_create_task(keyboard_main, 0) < 0)
        halt();
    if (ps2mouse_init() && scheduler_create_task(ps2mouse_main, 0) < 0)
        halt();

    serial_write_str("Kernel prepared well, scheduler start.\n\n");
    scheduler_run();

    halt();
}
