#pragma once

#include <common.h>

constexpr ui32 fat32_sector_size = 512;
constexpr i64 fat32_read_error = -1;

typedef bool (*fat32_read_sector_fn) (void *context, ui32 lba, ui8 *buffer);
typedef bool (*fat32_write_sector_fn) (void *context, ui32 lba, const ui8 *buffer);

struct fat32_filesystem_t {
    fat32_read_sector_fn read_sector;
    void *read_context;
    fat32_write_sector_fn write_sector;
    void *write_context;
    ui32 partition_lba;
    ui32 fat_start_lba;
    ui32 data_start_lba;
    ui32 sectors_per_fat;
    ui32 fat_count;
    ui32 root_cluster;
    ui32 cluster_count;
    ui8 sectors_per_cluster;
    bool mounted;
};

struct fat32_file_t {
    const fat32_filesystem_t *filesystem;
    ui32 first_cluster;
    ui32 size;
};

struct fat32_directory_entry_t {
    char name[13];
    ui8 attributes;
    ui32 first_cluster;
    ui32 size;
    fat32_directory_entry_t *next;
};

bool fat32_open(const fat32_filesystem_t *filesystem, const char *path, fat32_file_t *file);
i64 fat32_read(const fat32_file_t *file, ui32 offset, ui8 *buffer, ui32 size);
bool fat32_directory_exists(const fat32_filesystem_t *filesystem, const char *path);
fat32_directory_entry_t *fat32_list_directory(const fat32_filesystem_t *filesystem, const char *path);
void fat32_free_directory_list(fat32_directory_entry_t *entries);
