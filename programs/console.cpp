#include <program.h>

constexpr ui32 color_white = 0x00ffffff;
constexpr ui32 color_red   = 0x00f63c14;
constexpr ui32 color_green = 0x0048ef0e;
constexpr ui32 color_cyan  = 0x001cefba;
constexpr ui32 color_gray  = 0x00999999;
constexpr ui32 max_cmd_len  = 128;
constexpr ui32 max_path_len = 256;

static const program_api_t *system_api;
static char cwd[max_path_len] = "/";

static ui32 str_len(const char *text) {
    ui32 length = 0;
    while (text[length])
        length++;
    return length;
}

static bool str_eq(const char *left, const char *right) {
    while (*left && *right) {
        if (*left++ != *right++)
            return false;
    }
    return *left == *right;
}

static void str_copy(char *destination, const char *source, ui32 capacity) {
    ui32 index = 0;
    while (source[index] && index + 1 < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = 0;
}

static void str_concat(char *destination, const char *left, const char *right,
                       ui32 capacity) {
    str_copy(destination, left, capacity);
    ui32 position = str_len(destination);
    for (ui32 index = 0; right[index] && position + 1 < capacity; index++)
        destination[position++] = right[index];
    destination[position] = 0;
}

static void resolve_path(const char *input, char *output, ui32 capacity) {
    char segments[16][32];
    ui32 count = 0;
    const char *cursor = input;
    if (*cursor == '/')
        cursor++;
    while (*cursor && count < 16) {
        ui32 length = 0;
        while (*cursor && *cursor != '/' && length < 31)
            segments[count][length++] = *cursor++;
        segments[count][length] = 0;
        if (length && str_eq(segments[count], "..")) {
            if (count)
                count--;
        } else if (length && !str_eq(segments[count], ".")) {
            count++;
        }
        if (*cursor == '/')
            cursor++;
    }
    ui32 position = 0;
    output[position++] = '/';
    for (ui32 index = 0; index < count; index++) {
        for (ui32 offset = 0; segments[index][offset] && position + 1 < capacity;
             offset++)
            output[position++] = segments[index][offset];
        if (index + 1 < count && position + 1 < capacity)
            output[position++] = '/';
    }
    output[position] = 0;
}

static void resolve_from_cwd(const char *input, char *output) {
    char raw[max_path_len];
    if (input[0] == '/')
        str_copy(raw, input, max_path_len);
    else
        str_concat(raw, cwd, input, max_path_len);
    resolve_path(raw, output, max_path_len);
}

static void write_prompt() {
    system_api->write_console("[", color_gray);
    system_api->write_console(cwd, color_cyan);
    system_api->write_console("] ", color_gray);
}

static void cmd_help() {
    system_api->write_console("Available commands:\n", color_white);
    system_api->write_console("  help  - show this message\n", color_gray);
    system_api->write_console("  cd    - change directory\n", color_gray);
    system_api->write_console("  run   - run an ELF program\n", color_gray);
    system_api->write_console("  clear - clear screen\n", color_gray);
}

static void cmd_cd(const char *argument) {
    char path[max_path_len];
    resolve_from_cwd(argument[0] ? argument : "/", path);
    program_directory_entry_t probe[1];
    if (system_api->list_directory(path, probe, 1) < 0) {
        system_api->write_console("cd: no such directory: ", color_red);
        system_api->write_console(argument, color_red);
        system_api->write_console("\n", color_red);
        return;
    }
    str_copy(cwd, path, max_path_len);
    ui32 length = str_len(cwd);
    if (length > 1 && cwd[length - 1] != '/') {
        cwd[length] = '/';
        cwd[length + 1] = 0;
    }
}

static bool run_named_program(const char *command, const char *argument) {
    char path[max_path_len];
    str_copy(path, "/programs/", max_path_len);
    ui32 position = str_len(path);
    for (ui32 index = 0; command[index] && position + 5 < max_path_len; index++)
        path[position++] = command[index];
    str_copy(path + position, ".elf", max_path_len - position);
    char resolved_argument[max_path_len];
    resolve_from_cwd(argument, resolved_argument);
    return system_api->run_program(path, resolved_argument, cwd);
}

static void cmd_run(const char *argument) {
    if (!argument[0]) {
        system_api->write_console("run: missing program argument\n", color_red);
        return;
    }
    ui32 path_length = 0;
    while (argument[path_length] && argument[path_length] != ' ')
        path_length++;
    char path_argument[max_path_len];
    str_copy(path_argument, argument + path_length, max_path_len);
    while (path_argument[0] == ' ')
        str_copy(path_argument, path_argument + 1, max_path_len);
    char program_path[max_path_len];
    char raw_path[max_path_len];
    for (ui32 index = 0; index < path_length; index++)
        raw_path[index] = argument[index];
    raw_path[path_length] = 0;
    resolve_from_cwd(raw_path, program_path);
    char resolved_argument[max_path_len];
    resolve_from_cwd(path_argument, resolved_argument);
    if (!system_api->run_program(program_path, resolved_argument, cwd))
        system_api->write_console("run: cannot load program\n", color_red);
}

static void parse_and_exec(char *command) {
    while (*command == ' ')
        command++;
    ui32 length = str_len(command);
    while (length && command[length - 1] == ' ')
        command[--length] = 0;
    if (!length)
        return;
    char *argument = command;
    while (*argument && *argument != ' ')
        argument++;
    if (*argument) {
        *argument++ = 0;
        while (*argument == ' ')
            argument++;
    }
    if (str_eq(command, "help"))
        cmd_help();
    else if (str_eq(command, "cd"))
        cmd_cd(argument);
    else if (str_eq(command, "run"))
        cmd_run(argument);
    else if (str_eq(command, "clear"))
        system_api->clear_console();
    else if (!run_named_program(command, argument)) {
        system_api->write_console(command, color_red);
        system_api->write_console(": command not found\n", color_red);
    }
}

extern "C" void _start(const program_api_t *api) {
    system_api = api;
    api->clear_console();
    api->write_console("PatrickOS 2 Shell\n", color_green);
    api->write_console("Type 'help' for available commands.\n\n", color_gray);
    char line[max_cmd_len];
    ui32 position = 0;
    write_prompt();
    while (true) {
        char character;
        if (!api->read_input(&character)) {
            api->yield();
            continue;
        }
        if (character == '\n') {
            api->write_console("\n", color_white);
            line[position] = 0;
            parse_and_exec(line);
            position = 0;
            write_prompt();
        } else if (character == 0x08 && position > 0) {
            position--;
            api->erase_console_char();
        } else if (character >= ' ' && character < 127 &&
                   position + 1 < max_cmd_len) {
            line[position++] = character;
            char text[2] = {character, 0};
            api->write_console(text, color_white);
        }
        api->yield();
    }
}