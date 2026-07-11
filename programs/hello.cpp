#include <program.h>

extern "C" void _start(const program_api_t *api) {
    api->write_console("Hello from HELLO.ELF!\n", 0x00ff00);
}
