#include <graphics/font.h>
#include <graphics/basic.h>
#include <memory.h>
#include <scheduler.h>
#include <fs/fat32.h>

#include <algo/convert.h>

constexpr int max_line      = 48;  // 768 / 16
constexpr int line_max_char = 160; // 1280 / 8
constexpr ui32 fat32_directory_attribute = 0x10;
constexpr ui32 fat32_demo_path_size = 256;
constexpr ui32 fat32_demo_max_depth = 16;

ui16 current_line = 0, current_char = 0;

void clear_screen() {
    for (int x = 0; x <= 1280; x++) {
        for (int y = 0; y < 768; y++) {
            draw_pixel(x, y, 0);
        }
    }
}

static void prepare_cursor() {
    if (current_char >= line_max_char) {
        current_char = 0;
        current_line++;
    }

    if (current_line >= max_line) {
        clear_screen();
        current_line = 0;
        current_char = 0;
    }
}

void write_console(const char* str, ui32 color = COLOR_WHITE) {
    if (str == 0) {
        return;
    }

    for (const char* cursor = str; *cursor != 0; cursor++) {
        if (*cursor == '\n') {
            current_char = 0;
            current_line++;
        } else if (*cursor == '\t') {
            for (int spaces = 0; spaces < 4; spaces++) {
                prepare_cursor();
                current_char++;
            }
        } else {
            prepare_cursor();
            print_char(*cursor, current_char * 8, current_line * 16, color);
            current_char++;
        }
    }
}

static void write_memory_size(const char *label, ui64 size) {
    char buffer[21];
    write_console(label);
    write_console(int2str(size / (1024 * 1024), buffer));
    write_console(" MiB\n");
}

fat32_filesystem_t filesystem;

static void readfile_demo() {
    fat32_file_t file;
    char content[1024];

    if (!fat32_open(&filesystem, "/HELLO.TXT", &file)) {
        write_console("FAT32 HELLO.TXT not found\n");
        return;
    }

    i64 bytes_read = fat32_read(&file, 0, (ui8 *) content, sizeof(content) - 1);
    if (bytes_read < 0) {
        write_console("FAT32 HELLO.TXT read failed\n");
        return;
    }

    content[bytes_read] = 0;
    write_console("\nFAT32 HELLO.TXT:\n");
    write_console(content);
    write_console("\n");
}

static bool fat32_demo_make_path(const char *parent, const char *name,
                                 char path[fat32_demo_path_size]) {
    ui32 length = 0;
    while (parent[length] != 0) {
        if (length + 1 >= fat32_demo_path_size) return false;
        path[length] = parent[length];
        length++;
    }

    if (length == 0 || path[length - 1] != '/') {
        if (length + 1 >= fat32_demo_path_size) return false;
        path[length++] = '/';
    }

    for (ui32 index = 0; name[index] != 0; index++) {
        if (length + 1 >= fat32_demo_path_size) return false;
        path[length++] = name[index];
    }
    path[length] = 0;
    return true;
}

static void fat32_demo_wc_directory(const char *path, ui32 depth) {
    fat32_directory_entry_t *entries = fat32_list_directory(&filesystem, path);
    for (fat32_directory_entry_t *entry = entries; entry != 0; entry = entry->next) {
        for (ui32 indent = 0; indent < depth; indent++) write_console("  ");

        bool is_directory = (entry->attributes & fat32_directory_attribute) != 0;
        write_console(entry->name);
        write_console(is_directory ? "/\n" : "\n");

        if (!is_directory) continue;
        if (depth >= fat32_demo_max_depth) {
            for (ui32 indent = 0; indent <= depth; indent++) write_console("  ");
            write_console("<maximum depth reached>\n");
            continue;
        }

        char child_path[fat32_demo_path_size];
        if (!fat32_demo_make_path(path, entry->name, child_path)) {
            for (ui32 indent = 0; indent <= depth; indent++) write_console("  ");
            write_console("<path too long>\n");
            continue;
        }
        fat32_demo_wc_directory(child_path, depth + 1);
    }

    fat32_free_directory_list(entries);
}

static void readdir_demo() {
    write_console("\nList of / (recursive):\n");
    fat32_demo_wc_directory("/", 0);
    write_console("\n");
}

void console_main(void *arg) {
    (void) arg;

    if (!fat32_mount_primary_ata(&filesystem)) {
        write_console("FAT32 mount failed\n");
        return;
    }

    clear_screen();
    write_console("Welcome to PatrickOS 2 Console!\n");
    write_memory_size("Physical address range: ", memory_physical_size());
    write_memory_size("Usable physical memory: ", memory_usable_size());
    write_memory_size("Allocator free memory: ", memory_free_size());
    readfile_demo();
    readdir_demo();

    while (true) scheduler_yield();
}
