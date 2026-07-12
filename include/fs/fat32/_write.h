#pragma once

#include <fs/fat32/_read.h>

constexpr i64 fat32_write_error = -1;

bool fat32_create_file(fat32_filesystem_t *filesystem, const char *path);
bool fat32_create_directory(fat32_filesystem_t *filesystem, const char *path);
i64  fat32_write(fat32_filesystem_t *filesystem, const char *path, ui32 offset,
                 const ui8 *buffer, ui32 size);
bool fat32_rename(fat32_filesystem_t *filesystem, const char *path,
                  const char *new_path);
bool fat32_remove(fat32_filesystem_t *filesystem, const char *path);
