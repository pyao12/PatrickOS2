#include <program.h>

extern "C" void _start(const program_api_t *api) {
    ui8 buffer[1024];
    ui32 offset = 0;
    while (true) {
        i64 bytes_read = api->read_file(api->argument, offset, buffer, sizeof(buffer) - 1);
        if (bytes_read < 0) {
            api->write_console("cat: no such file\n", 0xff0000);
            return;
        }
        if (bytes_read == 0) return;
        buffer[bytes_read] = 0;
        api->write_console((const char *) buffer, 0xffffff);
        offset += (ui32) bytes_read;
        if ((ui32) bytes_read < sizeof(buffer) - 1) return;
    }
}
