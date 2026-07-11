#pragma once

#include <common.h>

typedef struct mb_info mb_info_t;

typedef struct mb_mmap_entry {
    ui32 size;
    ui64 addr;
    ui64 length;
    ui32 type;
} __attribute__ ((packed)) mb_mmap_entry_t;

void memory_init(const mb_info_t *mb_info);
void *memory_alloc_pages(ui64 count);
void memory_free_pages(void *address, ui64 count);
ui64 memory_physical_size();
ui64 memory_usable_size();
ui64 memory_free_size();
