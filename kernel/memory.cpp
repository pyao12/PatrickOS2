#include <memory.h>

#include <graphics/basic.h>

constexpr ui64 page_size = 4096;
constexpr ui64 page_mask = page_size - 1;
constexpr ui64 physical_memory_limit = 0x100000000ULL;
constexpr ui64 page_count = physical_memory_limit / page_size;
constexpr ui64 allocation_magic = 0x504f53324d454d31ULL;

struct mb_module_t {
    ui32 mod_start;
    ui32 mod_end;
    ui32 string;
    ui32 reserved;
} __attribute__ ((packed));

struct allocation_header_t {
    ui64 page_count;
    ui64 magic;
};

alignas(16) static ui8 page_bitmap[page_count / 8];
static bool memory_ready = false;

extern "C" ui8 _start;
extern "C" ui8 kernel_end;

static void mark_page_used(ui64 page) {
    page_bitmap[page / 8] |= (ui8) (1u << (page % 8));
}

static void mark_page_free(ui64 page) {
    page_bitmap[page / 8] &= (ui8) ~(1u << (page % 8));
}

static bool page_is_used(ui64 page) {
    return (page_bitmap[page / 8] & (ui8) (1u << (page % 8))) != 0;
}

static ui64 capped_range_end(ui64 start, ui64 length) {
    if (start >= physical_memory_limit || length > physical_memory_limit - start) {
        return physical_memory_limit;
    }
    return start + length;
}

static void mark_available_range(ui64 start, ui64 length) {
    ui64 end = capped_range_end(start, length);
    ui64 first = (start + page_mask) & ~page_mask;
    ui64 last = end & ~page_mask;

    for (ui64 page = first / page_size; page < last / page_size; page++) {
        mark_page_free(page);
    }
}

static void reserve_range(ui64 start, ui64 length) {
    if (start >= physical_memory_limit) return;

    ui64 end = capped_range_end(start, length);
    ui64 first = start & ~page_mask;
    ui64 last = end;
    if ((last & page_mask) != 0) last = (last + page_mask) & ~page_mask;
    if (last > physical_memory_limit) last = physical_memory_limit;

    for (ui64 page = first / page_size; page < last / page_size; page++) {
        mark_page_used(page);
    }
}

static void reserve_boot_data(const mb_info_t *mb_info) {
    reserve_range(0, 0x100000);
    reserve_range((ui64) (uip) &_start, (ui64) (uip) &kernel_end - (ui64) (uip) &_start);
    reserve_range((ui64) (uip) page_bitmap, sizeof(page_bitmap));
    reserve_range((ui64) (uip) mb_info, sizeof(mb_info_t));

    if ((mb_info->flags & (1u << 6)) != 0) {
        reserve_range(mb_info->mmap_addr, mb_info->mmap_length);
    }
    if ((mb_info->flags & (1u << 2)) != 0) reserve_range(mb_info->cmdline, 1);
    if ((mb_info->flags & (1u << 9)) != 0) reserve_range(mb_info->boot_loader_name, 1);

    if ((mb_info->flags & (1u << 3)) != 0) {
        reserve_range(mb_info->mods_addr, (ui64) mb_info->mods_count * sizeof(mb_module_t));
        const mb_module_t *modules = (const mb_module_t *) (uip) mb_info->mods_addr;
        for (ui32 index = 0; index < mb_info->mods_count; index++) {
            reserve_range(modules[index].mod_start, (ui64) modules[index].mod_end - modules[index].mod_start);
            reserve_range(modules[index].string, 1);
        }
    }

    if ((mb_info->flags & (1u << 12)) != 0) {
        reserve_range(mb_info->framebuffer_addr,
                      (ui64) mb_info->framebuffer_pitch * mb_info->framebuffer_height);
    }
}

static void *alloc_pages(ui64 count) {
    if (!memory_ready || count == 0 || count > page_count) return 0;

    ui64 consecutive = 0;
    for (ui64 page = 0; page < page_count; page++) {
        if (page_is_used(page)) {
            consecutive = 0;
            continue;
        }

        consecutive++;
        if (consecutive != count) continue;

        ui64 first = page + 1 - count;
        for (ui64 allocated_page = first; allocated_page <= page; allocated_page++) {
            mark_page_used(allocated_page);
        }
        return (void *) (uip) (first * page_size);
    }
    return 0;
}

static void free_pages(void *address, ui64 count) {
    ui64 start = (ui64) (uip) address;
    if (!memory_ready || address == 0 || count == 0 || (start & page_mask) != 0 ||
        start >= physical_memory_limit || count > (physical_memory_limit - start) / page_size) {
        halt();
    }

    ui64 first = start / page_size;
    for (ui64 page = first; page < first + count; page++) {
        mark_page_free(page);
    }
}

static void *kmalloc(uip size) {
    if (size == 0 || size > (uip) -1 - sizeof(allocation_header_t)) return 0;

    uip total_size = size + sizeof(allocation_header_t);
    ui64 pages = (total_size + page_mask) / page_size;
    allocation_header_t *header = (allocation_header_t *) alloc_pages(pages);
    if (header == 0) return 0;

    header->page_count = pages;
    header->magic = allocation_magic;
    return header + 1;
}

static void kfree(void *address) {
    if (address == 0) return;

    allocation_header_t *header = (allocation_header_t *) address - 1;
    if (header->magic != allocation_magic) halt();

    ui64 pages = header->page_count;
    header->magic = 0;
    free_pages(header, pages);
}

void memory_init(const mb_info_t *mb_info) {
    if (memory_ready || mb_info == 0 || (mb_info->flags & (1u << 6)) == 0 ||
        mb_info->mmap_addr == 0 || mb_info->mmap_length == 0) {
        halt();
    }

    for (ui64 index = 0; index < sizeof(page_bitmap); index++) {
        page_bitmap[index] = 0xff;
    }

    const ui8 *cursor = (const ui8 *) (uip) mb_info->mmap_addr;
    const ui8 *end = cursor + mb_info->mmap_length;
    while (cursor < end) {
        if ((ui64) (end - cursor) < sizeof(mb_mmap_entry_t)) halt();

        const mb_mmap_entry_t *entry = (const mb_mmap_entry_t *) cursor;
        ui64 record_length = (ui64) entry->size + sizeof(entry->size);
        if (record_length < sizeof(mb_mmap_entry_t) || record_length > (ui64) (end - cursor)) halt();

        if (entry->type == 1) mark_available_range(entry->addr, entry->length);
        cursor += record_length;
    }

    reserve_boot_data(mb_info);
    memory_ready = true;
}

// 我们采用内外双操作的策略，内部用 kalloc kfree 操作内存，暴露给调用者 new/delete 关键字

void *operator new(uip size) {
    void *address = kmalloc(size);
    if (address == 0) halt();
    return address;
}

void *operator new[](uip size) {
    void *address = kmalloc(size);
    if (address == 0) halt();
    return address;
}

void operator delete(void *address) noexcept {
    kfree(address);
}

void operator delete[](void *address) noexcept {
    kfree(address);
}

void operator delete(void *address, uip) noexcept {
    kfree(address);
}

void operator delete[](void *address, uip) noexcept {
    kfree(address);
}
