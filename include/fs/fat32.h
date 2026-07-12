#pragma once

#include <fs/fat32/_read.h>
#include <fs/fat32/_write.h>

bool fat32_mount(fat32_filesystem_t  *filesystem,
                 fat32_read_sector_fn read_sector, void *read_context,
                 ui32 partition_lba, fat32_write_sector_fn write_sector = 0,
                 void *write_context = 0);
bool fat32_mount_primary_ata(fat32_filesystem_t *filesystem);
bool fat32_mount_primary_ata();
bool fat32_open(const char *path, fat32_file_t *file);
bool fat32_directory_exists(const char *path);
fat32_directory_entry_t *fat32_list_directory(const char *path);
bool                     fat32_create_file(const char *path);
bool                     fat32_create_directory(const char *path);
i64  fat32_write(const char *path, ui32 offset, const ui8 *buffer, ui32 size);
bool fat32_rename(const char *path, const char *new_path);
bool fat32_remove(const char *path);
