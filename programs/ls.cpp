#include <program.h>

extern "C" void _start(const program_api_t *api) {
    program_directory_entry_t entries[64];
    i64 count = api->list_directory(api->argument, entries, 64);
    if (count < 0) {
        api->write_console("ls: cannot list directory\n", 0xff0000);
        return;
    }

    for (i64 index = 0; index < count; index++) {
        bool is_directory = (entries[index].attributes & program_directory_attribute_directory) != 0;
        api->write_console(entries[index].name, is_directory ? 0x00ffff : 0xffffff);
        api->write_console(is_directory ? "/  " : "  ", 0xffffff);
    }
    api->write_console("\n", 0xffffff);
}
