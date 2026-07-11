#include <program.h>

namespace {

bool split_two_args(const char *text, char *left, char *right, ui32 capacity) {
    ui32 in = 0;
    while (text[in] == ' ') in++;

    ui32 out = 0;
    while (text[in] != 0 && text[in] != ' ' && out + 1 < capacity) left[out++] = text[in++];
    left[out] = 0;
    while (text[in] == ' ') in++;

    out = 0;
    while (text[in] != 0 && out + 1 < capacity) right[out++] = text[in++];
    right[out] = 0;
    return left[0] != 0 && right[0] != 0;
}

ui32 str_len(const char *text) {
    ui32 length = 0;
    while (text[length] != 0) length++;
    return length;
}

bool str_eq(const char *a, const char *b) {
    while (*a != 0 && *b != 0) {
        if (*a != *b) return false;
        a++;
        b++;
    }
    return *a == *b;
}

void str_copy(char *dst, const char *src, ui32 capacity) {
    ui32 index = 0;
    while (src[index] != 0 && index + 1 < capacity) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = 0;
}

void str_concat(char *dst, const char *left, const char *right, ui32 capacity) {
    ui32 out = 0;
    while (left[out] != 0 && out + 1 < capacity) {
        dst[out] = left[out];
        out++;
    }
    ui32 in = 0;
    while (right[in] != 0 && out + 1 < capacity) dst[out++] = right[in++];
    dst[out] = 0;
}

void resolve_path(const char *cwd, const char *path, char *out, ui32 capacity) {
    char raw[256];
    if (path[0] == '/') str_copy(raw, path, sizeof(raw));
    else str_concat(raw, cwd != 0 ? cwd : "/", path, sizeof(raw));

    char segments[16][32];
    ui32 count = 0;
    const char *cursor = raw;
    if (*cursor == '/') cursor++;
    while (*cursor != 0 && count < 16) {
        ui32 length = 0;
        while (*cursor != 0 && *cursor != '/' && length + 1 < sizeof(segments[0])) {
            segments[count][length++] = *cursor++;
        }
        segments[count][length] = 0;
        if (length != 0) {
            if (str_eq(segments[count], ".")) {
            } else if (str_eq(segments[count], "..")) {
                if (count != 0) count--;
            } else {
                count++;
            }
        }
        if (*cursor == '/') cursor++;
    }

    out[0] = '/';
    ui32 pos = 1;
    for (ui32 index = 0; index < count; index++) {
        ui32 length = str_len(segments[index]);
        if (pos + length + 1 >= capacity) break;
        for (ui32 byte_index = 0; byte_index < length; byte_index++) out[pos++] = segments[index][byte_index];
        if (index + 1 < count) out[pos++] = '/';
    }
    out[pos] = 0;
}

}

extern "C" void _start(const program_api_t *api) {
    char path[256];
    char text[256];
    if (api->argument == 0 || !split_two_args(api->argument, path, text, sizeof(path))) {
        api->write_console("write: usage write <path> <text>\n", 0xff0000);
        return;
    }

    char resolved[256];
    resolve_path(api->cwd, path, resolved, sizeof(resolved));
    i64 result = api->write_file(resolved, 0, (const ui8 *) text, str_len(text));
    if (result < 0) api->write_console("write: write failed\n", 0xff0000);
}
