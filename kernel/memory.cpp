#include <memory.h>

#include <graphics/basic.h>

constexpr ui64 page_size             = 4096;
constexpr ui64 page_mask             = page_size - 1;
constexpr ui64 physical_memory_limit = 0x0000800000000000ULL;
constexpr ui64 allocation_magic      = 0x504f53324d454d31ULL;

struct mb_module_t {
    ui32 mod_start;
    ui32 mod_end;
    ui32 string;
    ui32 reserved;
} __attribute__((packed));

struct allocation_header_t {
    ui64 page_count;
    ui64 magic;
};

static ui8 *page_bitmap       = 0;
static ui64 page_count        = 0;
static ui64 usable_page_count = 0;
static ui64 free_page_count   = 0;
static bool memory_ready      = false;

extern "C" ui8 kernel_start;
extern "C" ui8 kernel_end;

static void mark_page_used(ui64 page) { page_bitmap[page / 8] |= (ui8)(1u << (page % 8)); }

static void mark_page_free(ui64 page) { page_bitmap[page / 8] &= (ui8) ~(1u << (page % 8)); }

static bool page_is_used(ui64 page) { return (page_bitmap[page / 8] & (ui8)(1u << (page % 8))) != 0; }

static ui64 range_end(ui64 start, ui64 length) {
    if (start > physical_memory_limit || length > physical_memory_limit - start) {
        halt();
    }
    return start + length;
}

static void mark_available_range(ui64 start, ui64 length) {
    ui64 end   = range_end(start, length);
    ui64 first = (start + page_mask) & ~page_mask;
    ui64 last  = end & ~page_mask;

    for (ui64 page = first / page_size; page < last / page_size; page++) {
        if (page_is_used(page)) {
            mark_page_free(page);
            free_page_count++;
        }
    }
}

static void reserve_range(ui64 start, ui64 length) {
    if (start >= physical_memory_limit)
        return;

    ui64 end   = range_end(start, length);
    ui64 first = start & ~page_mask;
    ui64 last  = end;
    if ((last & page_mask) != 0)
        last = (last + page_mask) & ~page_mask;
    if (last > physical_memory_limit)
        last = physical_memory_limit;

    for (ui64 page = first / page_size; page < last / page_size; page++) {
        if (!page_is_used(page)) {
            mark_page_used(page);
            free_page_count--;
        }
    }
}

static void reserve_boot_data(const mb_info_t *mb_info) {
    reserve_range(0, 0x100000);
    reserve_range((ui64)(uip)&kernel_start, (ui64)(uip)&kernel_end - (ui64)(uip)&kernel_start);
    reserve_range((ui64)(uip)page_bitmap, (page_count + 7) / 8);
    reserve_range((ui64)(uip)mb_info, sizeof(mb_info_t));

    if ((mb_info->flags & (1u << 6)) != 0) {
        reserve_range(mb_info->mmap_addr, mb_info->mmap_length);
    }
    if ((mb_info->flags & (1u << 2)) != 0)
        reserve_range(mb_info->cmdline, 1);
    if ((mb_info->flags & (1u << 9)) != 0)
        reserve_range(mb_info->boot_loader_name, 1);

    if ((mb_info->flags & (1u << 3)) != 0) {
        reserve_range(mb_info->mods_addr, (ui64)mb_info->mods_count * sizeof(mb_module_t));
        const mb_module_t *modules = (const mb_module_t *)(uip)mb_info->mods_addr;
        for (ui32 index = 0; index < mb_info->mods_count; index++) {
            reserve_range(modules[index].mod_start, (ui64)modules[index].mod_end - modules[index].mod_start);
            reserve_range(modules[index].string, 1);
        }
    }

    if ((mb_info->flags & (1u << 12)) != 0) {
        reserve_range(mb_info->framebuffer_addr, (ui64)mb_info->framebuffer_pitch * mb_info->framebuffer_height);
    }
}

static bool ranges_overlap(ui64 first_start, ui64 first_length, ui64 second_start, ui64 second_length) {
    if (first_length == 0 || second_length == 0)
        return false;

    ui64 first_end  = range_end(first_start, first_length);
    ui64 second_end = range_end(second_start, second_length);
    return first_start < second_end && second_start < first_end;
}

static bool overlaps_boot_data(const mb_info_t *mb_info, ui64 start, ui64 length) {
    if (ranges_overlap(start, length, 0, 0x100000) ||
        ranges_overlap(start, length, (ui64)(uip)&kernel_start, (ui64)(uip)&kernel_end - (ui64)(uip)&kernel_start) ||
        ranges_overlap(start, length, (ui64)(uip)mb_info, sizeof(mb_info_t)) ||
        ranges_overlap(start, length, mb_info->mmap_addr, mb_info->mmap_length)) {
        return true;
    }

    if ((mb_info->flags & (1u << 2)) != 0 && ranges_overlap(start, length, mb_info->cmdline, 1))
        return true;
    if ((mb_info->flags & (1u << 9)) != 0 && ranges_overlap(start, length, mb_info->boot_loader_name, 1))
        return true;
    if ((mb_info->flags & (1u << 12)) != 0 && ranges_overlap(start, length, mb_info->framebuffer_addr,
                                                             (ui64)mb_info->framebuffer_pitch * mb_info->framebuffer_height))
        return true;

    if ((mb_info->flags & (1u << 3)) == 0)
        return false;

    if (ranges_overlap(start, length, mb_info->mods_addr, (ui64)mb_info->mods_count * sizeof(mb_module_t)))
        return true;

    const mb_module_t *modules = (const mb_module_t *)(uip)mb_info->mods_addr;
    for (ui32 index = 0; index < mb_info->mods_count; index++) {
        if (ranges_overlap(start, length, modules[index].mod_start, (ui64)modules[index].mod_end - modules[index].mod_start) ||
            ranges_overlap(start, length, modules[index].string, 1)) {
            return true;
        }
    }
    return false;
}

static ui8 *find_bitmap_storage(const mb_info_t *mb_info, ui64 bitmap_size) {
    const ui8 *cursor         = (const ui8 *)(uip)mb_info->mmap_addr;
    const ui8 *end            = cursor + mb_info->mmap_length;
    ui64       storage_length = (bitmap_size + page_mask) & ~page_mask;

    while (cursor < end) {
        const mb_mmap_entry_t *entry         = (const mb_mmap_entry_t *)cursor;
        ui64                   record_length = (ui64)entry->size + sizeof(entry->size);
        ui64                   available_end = range_end(entry->addr, entry->length);
        if (entry->type == 1) {
            ui64 candidate = (entry->addr + page_mask) & ~page_mask;
            available_end &= ~page_mask;
            while (candidate < available_end && storage_length <= available_end - candidate) {
                if (!overlaps_boot_data(mb_info, candidate, storage_length)) {
                    return (ui8 *)(uip)candidate;
                }
                candidate += page_size;
            }
        }
        cursor += record_length;
    }
    return 0;
}

void *memory_alloc_pages(ui64 count) {
    if (!memory_ready || count == 0 || count > page_count)
        return 0;

    ui64 consecutive = 0;
    for (ui64 page = 0; page < page_count; page++) {
        if (page_is_used(page)) {
            consecutive = 0;
            continue;
        }

        consecutive++;
        if (consecutive != count)
            continue;

        ui64 first = page + 1 - count;
        for (ui64 allocated_page = first; allocated_page <= page; allocated_page++) {
            mark_page_used(allocated_page);
        }
        free_page_count -= count;
        return (void *)(uip)(first * page_size);
    }
    return 0;
}

void memory_free_pages(void *address, ui64 count) {
    ui64 start = (ui64)(uip)address;
    if (!memory_ready || address == 0 || count == 0 || (start & page_mask) != 0 || start >= physical_memory_limit ||
        count > (physical_memory_limit - start) / page_size) {
        halt();
    }

    ui64 first = start / page_size;
    for (ui64 page = first; page < first + count; page++) {
        mark_page_free(page);
    }
    free_page_count += count;
}

static void *kmalloc(uip size) {
    if (size == 0 || size > (uip)-1 - sizeof(allocation_header_t))
        return 0;

    uip                  total_size = size + sizeof(allocation_header_t);
    ui64                 pages      = (total_size + page_mask) / page_size;
    allocation_header_t *header     = (allocation_header_t *)memory_alloc_pages(pages);
    if (header == 0)
        return 0;

    header->page_count = pages;
    header->magic      = allocation_magic;
    return header + 1;
}

static void kfree(void *address) {
    if (address == 0)
        return;

    allocation_header_t *header = (allocation_header_t *)address - 1;
    if (header->magic != allocation_magic)
        halt();

    ui64 pages    = header->page_count;
    header->magic = 0;
    memory_free_pages(header, pages);
}

void memory_init(const mb_info_t *mb_info) {
    if (memory_ready || mb_info == 0 || (mb_info->flags & (1u << 6)) == 0 || mb_info->mmap_addr == 0 ||
        mb_info->mmap_length == 0) {
        halt();
    }

    ui64       highest_address = 0;
    const ui8 *cursor          = (const ui8 *)(uip)mb_info->mmap_addr;
    const ui8 *end             = cursor + mb_info->mmap_length;
    while (cursor < end) {
        if ((ui64)(end - cursor) < sizeof(mb_mmap_entry_t))
            halt();

        const mb_mmap_entry_t *entry         = (const mb_mmap_entry_t *)cursor;
        ui64                   record_length = (ui64)entry->size + sizeof(entry->size);
        if (record_length < sizeof(mb_mmap_entry_t) || record_length > (ui64)(end - cursor))
            halt();

        ui64 entry_end = range_end(entry->addr, entry->length);
        if (entry_end > highest_address)
            highest_address = entry_end;
        cursor += record_length;
    }

    page_count = (highest_address + page_mask) / page_size;
    if (page_count == 0)
        halt();

    ui64 bitmap_size = (page_count + 7) / 8;
    page_bitmap      = find_bitmap_storage(mb_info, bitmap_size);
    if (page_bitmap == 0)
        halt();

    for (ui64 index = 0; index < bitmap_size; index++) {
        page_bitmap[index] = 0xff;
    }

    cursor = (const ui8 *)(uip)mb_info->mmap_addr;
    end    = cursor + mb_info->mmap_length;
    while (cursor < end) {
        if ((ui64)(end - cursor) < sizeof(mb_mmap_entry_t))
            halt();

        const mb_mmap_entry_t *entry         = (const mb_mmap_entry_t *)cursor;
        ui64                   record_length = (ui64)entry->size + sizeof(entry->size);
        if (record_length < sizeof(mb_mmap_entry_t) || record_length > (ui64)(end - cursor))
            halt();

        if (entry->type == 1)
            mark_available_range(entry->addr, entry->length);
        cursor += record_length;
    }

    usable_page_count = free_page_count;
    reserve_boot_data(mb_info);
    memory_ready = true;
}

ui64 memory_physical_size() { return page_count * page_size; }

ui64 memory_usable_size() { return usable_page_count * page_size; }

ui64 memory_free_size() { return free_page_count * page_size; }

// 我们采用内外双操作的策略，内部用 kalloc kfree 操作内存，暴露给调用者
// new/delete 关键字

void *operator new(uip size) {
    void *address = kmalloc(size);
    if (address == 0)
        halt();
    return address;
}

void *operator new[](uip size) {
    void *address = kmalloc(size);
    if (address == 0)
        halt();
    return address;
}

void operator delete(void *address) noexcept { kfree(address); }

void operator delete[](void *address) noexcept { kfree(address); }

void operator delete(void *address, uip) noexcept { kfree(address); }

void operator delete[](void *address, uip) noexcept { kfree(address); }
