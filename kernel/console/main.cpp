#include <graphics/font.h>
#include <graphics/basic.h>
#include <memory.h>
#include <scheduler.h>
#include <fs/fat32.h>
#include <input.h>
#include <algo/convert.h>
#include <program.h>

constexpr int max_line      = 48;
constexpr int line_max_char = 160;
constexpr int max_cmd_len   = 128;
constexpr int max_path_len  = 256;

static ui16 current_line = 0, current_char = 0;
static char cwd[max_path_len] = "/";

static void clear_screen() {
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

void write_console(const char *str, ui32 color) {
    if (str == 0) return;
    for (const char *cursor = str; *cursor != 0; cursor++) {
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

static void write_prompt() {
    write_console("[", COLOR_GRAY);
    write_console(cwd, COLOR_CYAN);
    write_console("] ", COLOR_GRAY);
}

static ui32 str_len(const char *s) {
    ui32 len = 0;
    while (s[len]) len++;
    return len;
}

static bool str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, ui32 max) {
    ui32 i = 0;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void str_concat(char *dst, const char *a, const char *b, ui32 max) {
    ui32 i = 0;
    while (a[i] && i + 1 < max) {
        dst[i] = a[i];
        i++;
    }
    ui32 j = 0;
    while (b[j] && i + 1 < max) {
        dst[i++] = b[j++];
    }
    dst[i] = 0;
}

static void resolve_path(const char *in, char *out, ui32 max) {
    char segs[16][32];
    ui32 count = 0;

    const char *p = in;
    if (*p == '/') p++;

    while (*p && count < 16) {
        ui32 len = 0;
        while (*p && *p != '/' && len < 31) {
            segs[count][len++] = *p++;
        }
        segs[count][len] = 0;
        if (len > 0) {
            if (str_eq(segs[count], ".")) {
            } else if (str_eq(segs[count], "..")) {
                if (count > 0) count--;
            } else {
                count++;
            }
        }
        if (*p == '/') p++;
    }

    out[0] = '/';
    ui32 pos = 1;
    for (ui32 i = 0; i < count; i++) {
        ui32 slen = str_len(segs[i]);
        if (pos + slen + 2 > max) break;
        for (ui32 j = 0; j < slen; j++) out[pos++] = segs[i][j];
        if (i + 1 < count) out[pos++] = '/';
    }
    out[pos] = 0;
}

static void cmd_help() {
    write_console("Available commands:\n", COLOR_WHITE);
    write_console("  help  - show this message\n", COLOR_GRAY);
    write_console("  cd    - change directory\n", COLOR_GRAY);
    write_console("  ls    - list directory\n", COLOR_GRAY);
    write_console("  cat   - display file contents\n", COLOR_GRAY);
    write_console("  run   - run an ELF program\n", COLOR_GRAY);
    write_console("  clear - clear screen\n", COLOR_GRAY);
}

static void cmd_cd(const char *arg) {
    if (arg[0] == 0) {
        str_copy(cwd, "/", max_path_len);
        return;
    }

    char raw[max_path_len];
    if (arg[0] == '/') {
        str_copy(raw, arg, max_path_len);
    } else {
        str_concat(raw, cwd, arg, max_path_len);
    }

    char path[max_path_len];
    resolve_path(raw, path, max_path_len);

    if (!fat32_directory_exists(path)) {
        write_console("cd: no such directory: ", COLOR_RED);
        write_console(arg, COLOR_RED);
        write_console("\n", COLOR_RED);
        return;
    }

    fat32_directory_entry_t *entries = fat32_list_directory(path);
    fat32_free_directory_list(entries);
    str_copy(cwd, path, max_path_len);
    ui32 cwd_len = str_len(cwd);
    if (cwd_len > 1 && cwd[cwd_len - 1] != '/') {
        cwd[cwd_len] = '/';
        cwd[cwd_len + 1] = 0;
    }
}

static void cmd_cat(const char *arg) {
    if (arg[0] == 0) {
        write_console("cat: missing file argument\n", COLOR_RED);
        return;
    }

    char raw[max_path_len];
    if (arg[0] == '/') {
        str_copy(raw, arg, max_path_len);
    } else {
        str_concat(raw, cwd, arg, max_path_len);
    }

    char path[max_path_len];
    resolve_path(raw, path, max_path_len);

    if (!program_run("/programs/cat.elf", path, cwd)) write_console("cat: cannot load program\n", COLOR_RED);
}

static void cmd_ls() {
    if (!program_run("/programs/ls.elf", cwd, cwd)) write_console("ls: cannot load program\n", COLOR_RED);
}

static void cmd_clear() {
    clear_screen();
    current_line = 0;
    current_char = 0;
}

static void cmd_run(const char *arg) {
    if (arg[0] == 0) {
        write_console("run: missing program argument\n", COLOR_RED);
        return;
    }

    char raw[max_path_len];
    if (arg[0] == '/') str_copy(raw, arg, max_path_len);
    else str_concat(raw, cwd, arg, max_path_len);

    char path[max_path_len];
    resolve_path(raw, path, max_path_len);
    if (!program_run(path, "", cwd)) write_console("run: cannot load program\n", COLOR_RED);
}

static bool run_program_command(const char *cmd, const char *arg) {
    char path[max_path_len];
    str_copy(path, "/programs/", max_path_len);

    ui32 pos = str_len(path);
    for (ui32 index = 0; cmd[index] && pos + 5 < max_path_len; index++) {
        path[pos++] = cmd[index];
    }
    path[pos++] = '.';
    path[pos++] = 'e';
    path[pos++] = 'l';
    path[pos++] = 'f';
    path[pos] = 0;

    fat32_file_t file;
    if (!fat32_open(path, &file)) return false;
    char raw[max_path_len];
    if (arg[0] == '/') str_copy(raw, arg, max_path_len);
    else str_concat(raw, cwd, arg, max_path_len);

    char resolved[max_path_len];
    resolve_path(raw, resolved, max_path_len);
    return program_run(path, resolved, cwd);
}

static void parse_and_exec(char *cmd) {
    while (*cmd == ' ') cmd++;
    ui32 len = str_len(cmd);
    while (len > 0 && cmd[len - 1] == ' ') {
        cmd[--len] = 0;
    }

    if (len == 0) return;

    char *arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg == ' ') {
        *arg = 0;
        arg++;
        while (*arg == ' ') arg++;
    } else {
        arg = cmd + len;
    }

    if (str_eq(cmd, "help")) {
        cmd_help();
    } else if (str_eq(cmd, "cd")) {
        cmd_cd(arg);
    } else if (str_eq(cmd, "ls")) {
        cmd_ls();
    } else if (str_eq(cmd, "cat")) {
        cmd_cat(arg);
    } else if (str_eq(cmd, "run")) {
        cmd_run(arg);
    } else if (str_eq(cmd, "clear")) {
        cmd_clear();
    } else if (run_program_command(cmd, arg)) {
    } else {
        write_console(cmd, COLOR_RED);
        write_console(": command not found\n", COLOR_RED);
    }
}

void console_main(void *arg) {
    (void) arg;
    clear_screen();
    write_console("PatrickOS 2 Shell\n", COLOR_GREEN);
    write_console("Type 'help' for available commands.\n\n", COLOR_GRAY);

    char line_buf[max_cmd_len];
    int line_pos = 0;

    write_prompt();

    while (true) {
        char c;
        if (!input_buffer_pop(&global_input_buffer, &c)) {
            scheduler_yield();
            continue;
        }

        if (c == '\n') {
            write_console("\n", COLOR_WHITE);
            line_buf[line_pos] = 0;
            parse_and_exec(line_buf);
            line_pos = 0;
            write_prompt();
        } else if (c == 0x08) {
            if (line_pos > 0) {
                line_pos--;
                current_char--;
                prepare_cursor();
                print_char(' ', current_char * 8, current_line * 16, COLOR_WHITE);
            }
        } else if (c >= ' ' && c < 127 && line_pos < max_cmd_len - 1) {
            line_buf[line_pos++] = c;
            char buf[2] = { c, 0 };
            write_console(buf, COLOR_WHITE);
        }

        scheduler_yield();
    }
}
