#pragma once

#include <fs/fat32/_read.h>

bool fat32_mount(fat32_filesystem_t *filesystem, fat32_read_sector_fn read_sector,
                void *read_context, ui32 partition_lba);
bool fat32_mount_primary_ata(fat32_filesystem_t *filesystem);
