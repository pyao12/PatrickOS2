#pragma once

#include <fs/fat32/_read.h>

bool fat32_mount(fat32_filesystem_t *filesystem, fat32_read_sector_fn read_sector, void *read_context, ui32 partition_lba);
bool fat32_mount_primary_ata(fat32_filesystem_t *filesystem);
bool fat32_mount_primary_ata();
bool fat32_open(const char *path, fat32_file_t *file);
fat32_directory_entry_t *fat32_list_directory(const char *path);
