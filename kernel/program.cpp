#include <console.h>
#include <fs/fat32.h>
#include <input.h>
#include <memory.h>
#include <program.h>
#include <scheduler.h>
#include <x86.h>

namespace {

constexpr ui8  elf_class_64            = 2;
constexpr ui8  elf_data_little_endian  = 1;
constexpr ui8  elf_version_current     = 1;
constexpr ui16 elf_type_dynamic        = 3;
constexpr ui16 elf_machine_x86_64      = 62;
constexpr ui32 elf_program_load        = 1;
constexpr ui32 elf_program_dynamic     = 2;
constexpr ui32 elf_program_interpreter = 3;
constexpr ui32 elf_program_tls         = 7;
constexpr ui32 elf_segment_executable  = 1;
constexpr ui32 elf_segment_writable    = 2;
constexpr ui64 elf_dynamic_needed      = 1;
constexpr ui64 elf_dynamic_rela        = 7;
constexpr ui64 elf_dynamic_rel         = 17;
constexpr ui64 elf_dynamic_jmprel      = 23;
constexpr ui64 page_size               = 4096;
constexpr ui64 max_program_size        = 16 * 1024 * 1024;
constexpr ui16 max_program_headers     = 64;
constexpr ui64 user_base               = 0x0000400000000000ULL;
constexpr ui64 user_stack_address = user_base + max_program_size + page_size;
constexpr ui64 user_api_address   = user_stack_address + 2 * page_size;
constexpr ui64 page_address_mask  = 0x000ffffffffff000ULL;
constexpr ui64 page_present       = 1;
constexpr ui64 page_writable      = 2;
constexpr ui64 page_user          = 4;
constexpr ui64 page_no_execute    = 1ULL << 63;
constexpr ui64 syscall_exit       = 0;
constexpr ui64 syscall_write      = 1;
constexpr ui64 syscall_yield      = 2;
constexpr ui64 syscall_read_file  = 3;
constexpr ui64 syscall_list_directory   = 4;
constexpr ui64 syscall_create_file      = 5;
constexpr ui64 syscall_create_directory = 6;
constexpr ui64 syscall_write_file       = 7;
constexpr ui64 syscall_rename_path      = 8;
constexpr ui64 syscall_remove_path      = 9;
constexpr ui64 syscall_read_input       = 10;
constexpr ui64 syscall_clear_console    = 11;
constexpr ui64 syscall_erase_console    = 12;
constexpr ui64 syscall_run_program      = 13;
constexpr ui64 syscall_result_exit      = 0x100;
constexpr ui64 syscall_result_failure   = 0x101;
constexpr ui64 max_syscall_text         = 1024;
constexpr ui64 max_syscall_path         = 256;
constexpr ui64 max_syscall_io           = 4096;
constexpr ui64 max_page_tables          = 16;

struct elf64_header_t {
    ui8  ident[16];
    ui16 type;
    ui16 machine;
    ui32 version;
    ui64 entry;
    ui64 program_header_offset;
    ui64 section_header_offset;
    ui32 flags;
    ui16 header_size;
    ui16 program_header_size;
    ui16 program_header_count;
    ui16 section_header_size;
    ui16 section_header_count;
    ui16 section_name_index;
} __attribute__((packed));

struct elf64_program_header_t {
    ui32 type;
    ui32 flags;
    ui64 offset;
    ui64 virtual_address;
    ui64 physical_address;
    ui64 file_size;
    ui64 memory_size;
    ui64 alignment;
} __attribute__((packed));

struct elf64_dynamic_t {
    ui64 tag;
    ui64 value;
} __attribute__((packed));

struct program_address_space_t {
    ui64 *pml4;
    ui8  *image;
    ui64  image_pages;
    ui8  *stack;
    ui8  *api;
    void *page_tables[max_page_tables];
    ui64  page_table_count;
};

program_address_space_t *active_program = 0;

bool range_fits(ui64 offset, ui64 size, ui64 limit) {
    return offset <= limit && size <= limit - offset;
}

bool read_exact(const fat32_file_t &file, ui64 offset, void *buffer,
                ui64 size) {
    if (!range_fits(offset, size, file.size) || size > 0xffffffffu)
        return false;
    return fat32_read(&file, (ui32)offset, (ui8 *)buffer, (ui32)size) ==
           (i64)size;
}

bool read_program_header(const fat32_file_t &file, const elf64_header_t &header,
                         ui16 index, elf64_program_header_t *program_header) {
    ui64 offset =
        header.program_header_offset + (ui64)index * sizeof(*program_header);
    return read_exact(file, offset, program_header, sizeof(*program_header));
}

bool dynamic_segment_supported(const fat32_file_t           &file,
                               const elf64_program_header_t &program_header) {
    if (!range_fits(program_header.offset, program_header.file_size,
                    file.size) ||
        program_header.file_size % sizeof(elf64_dynamic_t) != 0)
        return false;

    ui64 count = program_header.file_size / sizeof(elf64_dynamic_t);
    for (ui64 index = 0; index < count; index++) {
        elf64_dynamic_t dynamic;
        if (!read_exact(file, program_header.offset + index * sizeof(dynamic),
                        &dynamic, sizeof(dynamic)))
            return false;
        if (dynamic.tag == elf_dynamic_needed ||
            dynamic.tag == elf_dynamic_rela || dynamic.tag == elf_dynamic_rel ||
            dynamic.tag == elf_dynamic_jmprel)
            return false;
    }
    return true;
}

void zero_pages(void *address, ui64 count) {
    ui8 *bytes = (ui8 *)address;
    for (ui64 index = 0; index < count * page_size; index++)
        bytes[index] = 0;
}

ui64 *allocate_page_table(program_address_space_t *program) {
    if (program->page_table_count == max_page_tables)
        return 0;
    ui64 *table = (ui64 *)memory_alloc_pages(1);
    if (table == 0)
        return 0;
    zero_pages(table, 1);
    program->page_tables[program->page_table_count++] = table;
    return table;
}

ui64 *next_page_table(program_address_space_t *program, ui64 *table,
                      ui64 index) {
    if ((table[index] & page_present) != 0) {
        if ((table[index] & (1ULL << 7)) != 0)
            return 0;
        return (ui64 *)(uip)(table[index] & page_address_mask);
    }

    ui64 *next = allocate_page_table(program);
    if (next == 0)
        return 0;
    table[index] = (ui64)(uip)next | page_present | page_writable | page_user;
    return next;
}

bool map_user_page(program_address_space_t *program, ui64 virtual_address,
                   ui64 physical_address, bool writable, bool executable) {
    ui64 *pdpt = next_page_table(program, program->pml4,
                                 (virtual_address >> 39) & 0x1ff);
    if (pdpt == 0)
        return false;
    ui64 *pd = next_page_table(program, pdpt, (virtual_address >> 30) & 0x1ff);
    if (pd == 0)
        return false;
    ui64 *pt = next_page_table(program, pd, (virtual_address >> 21) & 0x1ff);
    if (pt == 0)
        return false;

    ui64 &entry = pt[(virtual_address >> 12) & 0x1ff];
    ui64  flags = page_present | page_user | (writable ? page_writable : 0) |
                 (executable ? 0 : page_no_execute);
    ui64 value = (physical_address & page_address_mask) | flags;
    if ((entry & page_present) != 0)
        return entry == value;
    entry = value;
    return true;
}

bool user_address_mapped(const program_address_space_t *program, ui64 address) {
    const ui64 *table      = program->pml4;
    const ui16  indices[4] = {
        (ui16)((address >> 39) & 0x1ff), (ui16)((address >> 30) & 0x1ff),
        (ui16)((address >> 21) & 0x1ff), (ui16)((address >> 12) & 0x1ff)};
    for (ui8 level = 0; level < 4; level++) {
        ui64 entry = table[indices[level]];
        if ((entry & (page_present | page_user)) != (page_present | page_user))
            return false;
        if (level == 3)
            return true;
        if ((entry & (1ULL << 7)) != 0)
            return false;
        table = (const ui64 *)(uip)(entry & page_address_mask);
    }
    return false;
}

bool user_range_mapped(const program_address_space_t *program, ui64 address,
                       ui64 size) {
    if (size == 0)
        return true;
    if (address > (ui64)-1 - (size - 1))
        return false;
    ui64 end = address + size - 1;
    for (ui64 page = address & ~(page_size - 1); page <= end;
         page += page_size) {
        if (!user_address_mapped(program, page))
            return false;
        if (page > (ui64)-1 - page_size)
            return false;
    }
    return true;
}

bool copy_user_string(const char *user_text, char *text, ui64 capacity) {
    for (ui64 index = 0; index < capacity; index++) {
        ui64 address = (ui64)(uip)(user_text + index);
        if (!user_address_mapped(active_program, address))
            return false;
        text[index] = user_text[index];
        if (text[index] == 0)
            return true;
    }
    return false;
}

void destroy_address_space(program_address_space_t *program) {
    for (ui64 index = 0; index < program->page_table_count; index++) {
        memory_free_pages(program->page_tables[index], 1);
    }
    if (program->pml4 != 0)
        memory_free_pages(program->pml4, 1);
    if (program->api != 0)
        memory_free_pages(program->api, 1);
    if (program->stack != 0)
        memory_free_pages(program->stack, 2);
    if (program->image != 0)
        memory_free_pages(program->image, program->image_pages);
}

} // namespace

extern "C" ui64 program_syscall(ui64 number, ui64 argument1, ui64 argument2,
                                ui64 argument3, ui64 argument4) {
    if (active_program == 0)
        return syscall_result_failure;
    if (number == syscall_exit)
        return syscall_result_exit;
    if (number == syscall_yield) {
        scheduler_yield();
        return 0;
    }
    if (number == syscall_write) {
        char text[max_syscall_text];
        if (!copy_user_string((const char *)(uip)argument1, text,
                              sizeof(text))) {
            return syscall_result_failure;
        }
        write_console(text, (ui32)argument2);
        return 0;
    }
    if (number == syscall_read_input) {
        if (!user_range_mapped(active_program, argument1, 1))
            return syscall_result_failure;
        return input_buffer_pop(&global_input_buffer, (char *)(uip)argument1);
    }
    if (number == syscall_clear_console) {
        clear_console();
        return 0;
    }
    if (number == syscall_erase_console) {
        erase_console_char();
        return 0;
    }
    if (number == syscall_run_program) {
        char program_path[max_syscall_path];
        char program_argument[max_syscall_path];
        char program_cwd[max_syscall_path];
        if (!copy_user_string((const char *)(uip)argument1, program_path,
                              sizeof(program_path)) ||
            !copy_user_string((const char *)(uip)argument2, program_argument,
                              sizeof(program_argument)) ||
            !copy_user_string((const char *)(uip)argument3, program_cwd,
                              sizeof(program_cwd)))
            return syscall_result_failure;
        program_address_space_t *parent = active_program;
        bool success = program_run(program_path, program_argument, program_cwd);
        active_program = parent;
        return success;
    }

    char path[max_syscall_path];
    if ((number != syscall_read_file && number != syscall_list_directory &&
         number != syscall_create_file && number != syscall_create_directory &&
         number != syscall_write_file && number != syscall_rename_path &&
         number != syscall_remove_path) ||
        !copy_user_string((const char *)(uip)argument1, path, sizeof(path))) {
        return syscall_result_failure;
    }

    if (number == syscall_read_file) {
        if (argument2 > 0xffffffffu || argument4 > max_syscall_io ||
            !user_range_mapped(active_program, argument3, argument4))
            return syscall_result_failure;
        fat32_file_t file;
        if (!fat32_open(path, &file))
            return (ui64)-1;
        return (ui64)fat32_read(&file, (ui32)argument2, (ui8 *)(uip)argument3,
                                (ui32)argument4);
    }

    if (number == syscall_create_file)
        return fat32_create_file(path) ? 0 : (ui64)-1;
    if (number == syscall_create_directory)
        return fat32_create_directory(path) ? 0 : (ui64)-1;
    if (number == syscall_remove_path)
        return fat32_remove(path) ? 0 : (ui64)-1;

    if (number == syscall_rename_path) {
        char new_path[max_syscall_path];
        if (!copy_user_string((const char *)(uip)argument2, new_path,
                              sizeof(new_path))) {
            return syscall_result_failure;
        }
        return fat32_rename(path, new_path) ? 0 : (ui64)-1;
    }

    if (number == syscall_write_file) {
        if (argument2 > 0xffffffffu || argument4 > max_syscall_io ||
            !user_range_mapped(active_program, argument3, argument4))
            return syscall_result_failure;
        return (ui64)fat32_write(path, (ui32)argument2,
                                 (const ui8 *)(uip)argument3, (ui32)argument4);
    }

    if (argument3 > 64 ||
        !user_range_mapped(active_program, argument2,
                           argument3 * sizeof(program_directory_entry_t))) {
        return syscall_result_failure;
    }
    fat32_directory_entry_t *entries = fat32_list_directory(path);
    if (entries == 0 && !fat32_directory_exists(path))
        return (ui64)-1;
    ui64                       count = 0;
    program_directory_entry_t *user_entries =
        (program_directory_entry_t *)(uip)argument2;
    for (fat32_directory_entry_t *entry         = entries;
         entry != 0 && count < argument3; entry = entry->next, count++) {
        for (ui64 index = 0; index < sizeof(entry->name); index++) {
            user_entries[count].name[index] = entry->name[index];
        }
        user_entries[count].attributes = entry->attributes;
    }
    fat32_free_directory_list(entries);
    return count;
}

extern "C" [[noreturn]] void program_exception(ui64, ui64, ui64 code_selector) {
    if (active_program != 0 && (code_selector & 3) == 3)
        x86_leave_user(false);
    halt();
    __builtin_unreachable();
}

bool program_run(const char *path, const char *argument, const char *cwd) {
    fat32_file_t   file;
    elf64_header_t header;
    if (path == 0 || !fat32_open(path, &file) ||
        !read_exact(file, 0, &header, sizeof(header)))
        return false;

    if (header.ident[0] != 0x7f || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != elf_class_64 ||
        header.ident[5] != elf_data_little_endian ||
        header.ident[6] != elf_version_current ||
        header.type != elf_type_dynamic ||
        header.machine != elf_machine_x86_64 ||
        header.version != elf_version_current ||
        header.header_size != sizeof(header) ||
        header.program_header_size != sizeof(elf64_program_header_t) ||
        header.program_header_count == 0 ||
        header.program_header_count > max_program_headers ||
        !range_fits(header.program_header_offset,
                    (ui64)header.program_header_count *
                        sizeof(elf64_program_header_t),
                    file.size))
        return false;

    ui64 lowest_address      = (ui64)-1;
    ui64 highest_address     = 0;
    bool entry_is_executable = false;
    for (ui16 index = 0; index < header.program_header_count; index++) {
        elf64_program_header_t program_header;
        if (!read_program_header(file, header, index, &program_header))
            return false;
        if (program_header.type == elf_program_interpreter ||
            program_header.type == elf_program_tls)
            return false;
        if (program_header.type == elf_program_dynamic &&
            !dynamic_segment_supported(file, program_header))
            return false;
        if (program_header.type != elf_program_load)
            continue;

        if (program_header.file_size > program_header.memory_size ||
            !range_fits(program_header.offset, program_header.file_size,
                        file.size) ||
            (program_header.alignment != 0 &&
             ((program_header.alignment & (program_header.alignment - 1)) !=
                  0 ||
              ((program_header.virtual_address - program_header.offset) &
               (program_header.alignment - 1)) != 0)) ||
            program_header.virtual_address >
                (ui64)-1 - program_header.memory_size)
            return false;

        ui64 segment_start = program_header.virtual_address & ~(page_size - 1);
        ui64 segment_end_unaligned =
            program_header.virtual_address + program_header.memory_size;
        if (segment_end_unaligned > (ui64)-1 - (page_size - 1))
            return false;
        ui64 segment_end =
            (segment_end_unaligned + page_size - 1) & ~(page_size - 1);
        if (segment_start < lowest_address)
            lowest_address = segment_start;
        if (segment_end > highest_address)
            highest_address = segment_end;
        if ((program_header.flags & elf_segment_executable) != 0 &&
            header.entry >= program_header.virtual_address &&
            header.entry <
                program_header.virtual_address + program_header.memory_size) {
            entry_is_executable = true;
        }
    }

    if (!entry_is_executable || lowest_address == (ui64)-1 ||
        highest_address <= lowest_address ||
        highest_address - lowest_address > max_program_size)
        return false;

    program_address_space_t program;
    program.pml4             = 0;
    program.image            = 0;
    program.image_pages      = 0;
    program.stack            = 0;
    program.api              = 0;
    program.page_table_count = 0;
    ui64 image_size          = highest_address - lowest_address;
    program.image_pages      = image_size / page_size;
    program.image            = (ui8 *)memory_alloc_pages(program.image_pages);
    program.stack            = (ui8 *)memory_alloc_pages(2);
    program.api              = (ui8 *)memory_alloc_pages(1);
    program.pml4             = (ui64 *)memory_alloc_pages(1);
    if (program.image == 0 || program.stack == 0 || program.api == 0 ||
        program.pml4 == 0) {
        destroy_address_space(&program);
        return false;
    }
    zero_pages(program.image, program.image_pages);
    zero_pages(program.stack, 2);
    zero_pages(program.api, 1);

    ui64 kernel_page_table;
    asm("mov %%cr3, %0" : "=r"(kernel_page_table));
    const ui64 *kernel_pml4 =
        (const ui64 *)(uip)(kernel_page_table & page_address_mask);
    for (ui16 index = 0; index < 512; index++)
        program.pml4[index] = kernel_pml4[index];
    program.pml4[(user_base >> 39) & 0x1ff] = 0;

    for (ui16 index = 0; index < header.program_header_count; index++) {
        elf64_program_header_t program_header;
        if (!read_program_header(file, header, index, &program_header) ||
            (program_header.type == elf_program_load &&
             !read_exact(file, program_header.offset,
                         program.image + program_header.virtual_address -
                             lowest_address,
                         program_header.file_size))) {
            destroy_address_space(&program);
            return false;
        }
        if (program_header.type != elf_program_load)
            continue;
        if ((program_header.flags &
             (elf_segment_executable | elf_segment_writable)) ==
            (elf_segment_executable | elf_segment_writable)) {
            destroy_address_space(&program);
            return false;
        }

        ui64 segment_start = program_header.virtual_address & ~(page_size - 1);
        ui64 segment_end   = (program_header.virtual_address +
                            program_header.memory_size + page_size - 1) &
                           ~(page_size - 1);
        for (ui64 address = segment_start; address < segment_end;
             address += page_size) {
            if (!map_user_page(
                    &program, user_base + address - lowest_address,
                    (ui64)(uip)program.image + address - lowest_address,
                    (program_header.flags & elf_segment_writable) != 0,
                    (program_header.flags & elf_segment_executable) != 0)) {
                destroy_address_space(&program);
                return false;
            }
        }
    }

    constexpr ui64 write_stub_offset            = 512;
    constexpr ui64 yield_stub_offset            = 528;
    constexpr ui64 read_file_stub_offset        = 544;
    constexpr ui64 list_directory_stub_offset   = 560;
    constexpr ui64 create_file_stub_offset      = 576;
    constexpr ui64 create_directory_stub_offset = 592;
    constexpr ui64 write_file_stub_offset       = 608;
    constexpr ui64 rename_path_stub_offset      = 624;
    constexpr ui64 remove_path_stub_offset      = 640;
    constexpr ui64 exit_stub_offset             = 656;
    constexpr ui64 read_input_stub_offset       = 672;
    constexpr ui64 clear_console_stub_offset    = 688;
    constexpr ui64 erase_console_stub_offset    = 704;
    constexpr ui64 run_program_stub_offset      = 720;
    constexpr ui64 argument_offset              = 128;
    constexpr ui64 cwd_offset                   = 384;
    program_api_t *api                          = (program_api_t *)program.api;
    api->write_console = (void (*)(const char *, ui32))(uip)(user_api_address +
                                                             write_stub_offset);
    api->yield     = (void (*)())(uip)(user_api_address + yield_stub_offset);
    api->argument  = (const char *)(uip)(user_api_address + argument_offset);
    api->cwd       = (const char *)(uip)(user_api_address + cwd_offset);
    api->read_file = (i64 (*)(const char *, ui32, ui8 *, ui32))(
        uip)(user_api_address + read_file_stub_offset);
    api->list_directory =
        (i64 (*)(const char *, program_directory_entry_t *, ui32))(
            uip)(user_api_address + list_directory_stub_offset);
    api->create_file      = (i64 (*)(const char *))(uip)(user_api_address +
                                                    create_file_stub_offset);
    api->create_directory = (i64 (*)(const char *))(
        uip)(user_api_address + create_directory_stub_offset);
    api->write_file = (i64 (*)(const char *, ui32, const ui8 *, ui32))(
        uip)(user_api_address + write_file_stub_offset);
    api->rename_path = (i64 (*)(const char *, const char *))(
        uip)(user_api_address + rename_path_stub_offset);
    api->remove_path = (i64 (*)(const char *))(uip)(user_api_address +
                                                    remove_path_stub_offset);
    api->read_input =
        (bool (*)(char *))(uip)(user_api_address + read_input_stub_offset);
    api->clear_console =
        (void (*)())(uip)(user_api_address + clear_console_stub_offset);
    api->erase_console_char =
        (void (*)())(uip)(user_api_address + erase_console_stub_offset);
    api->run_program = (bool (*)(const char *, const char *, const char *))(
        uip)(user_api_address + run_program_stub_offset);
    const ui8 write_stub[]            = {0xb8, 1, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 yield_stub[]            = {0xb8, 2, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 read_file_stub[]        = {0xb8, 3, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 list_directory_stub[]   = {0xb8, 4, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 create_file_stub[]      = {0xb8, 5, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 create_directory_stub[] = {0xb8, 6, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 write_file_stub[]       = {0xb8, 7, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 rename_path_stub[]      = {0xb8, 8, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 remove_path_stub[]      = {0xb8, 9, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 read_input_stub[]       = {0xb8, 10, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 clear_console_stub[]    = {0xb8, 11, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 erase_console_stub[]    = {0xb8, 12, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 run_program_stub[]      = {0xb8, 13, 0, 0, 0, 0xcd, 0x80, 0xc3};
    const ui8 exit_stub[]             = {0x31, 0xc0, 0xcd, 0x80, 0xf4};
    for (ui64 index = 0; index < sizeof(write_stub); index++)
        program.api[write_stub_offset + index] = write_stub[index];
    for (ui64 index = 0; index < sizeof(yield_stub); index++)
        program.api[yield_stub_offset + index] = yield_stub[index];
    for (ui64 index = 0; index < sizeof(read_file_stub); index++)
        program.api[read_file_stub_offset + index] = read_file_stub[index];
    for (ui64 index = 0; index < sizeof(list_directory_stub); index++)
        program.api[list_directory_stub_offset + index] =
            list_directory_stub[index];
    for (ui64 index = 0; index < sizeof(create_file_stub); index++)
        program.api[create_file_stub_offset + index] = create_file_stub[index];
    for (ui64 index = 0; index < sizeof(create_directory_stub); index++)
        program.api[create_directory_stub_offset + index] =
            create_directory_stub[index];
    for (ui64 index = 0; index < sizeof(write_file_stub); index++)
        program.api[write_file_stub_offset + index] = write_file_stub[index];
    for (ui64 index = 0; index < sizeof(rename_path_stub); index++)
        program.api[rename_path_stub_offset + index] = rename_path_stub[index];
    for (ui64 index = 0; index < sizeof(remove_path_stub); index++)
        program.api[remove_path_stub_offset + index] = remove_path_stub[index];
    for (ui64 index = 0; index < sizeof(read_input_stub); index++)
        program.api[read_input_stub_offset + index] = read_input_stub[index];
    for (ui64 index = 0; index < sizeof(clear_console_stub); index++)
        program.api[clear_console_stub_offset + index] =
            clear_console_stub[index];
    for (ui64 index = 0; index < sizeof(erase_console_stub); index++)
        program.api[erase_console_stub_offset + index] =
            erase_console_stub[index];
    for (ui64 index = 0; index < sizeof(run_program_stub); index++)
        program.api[run_program_stub_offset + index] = run_program_stub[index];
    for (ui64 index = 0; index < sizeof(exit_stub); index++)
        program.api[exit_stub_offset + index] = exit_stub[index];
    for (ui64 index = 0; argument != 0 && argument[index] != 0 &&
                         index + 1 < page_size - argument_offset;
         index++) {
        program.api[argument_offset + index] = argument[index];
    }
    for (ui64 index = 0;
         cwd != 0 && cwd[index] != 0 && index + 1 < page_size - cwd_offset;
         index++) {
        program.api[cwd_offset + index] = cwd[index];
    }

    ui64 user_stack_top = user_stack_address + 2 * page_size - sizeof(ui64);
    *(ui64 *)(program.stack + 2 * page_size - sizeof(ui64)) =
        user_api_address + exit_stub_offset;
    if (!map_user_page(&program, user_stack_address, (ui64)(uip)program.stack,
                       true, false) ||
        !map_user_page(&program, user_stack_address + page_size,
                       (ui64)(uip)program.stack + page_size, true, false) ||
        !map_user_page(&program, user_api_address, (ui64)(uip)program.api,
                       false, true)) {
        destroy_address_space(&program);
        return false;
    }

    active_program = &program;
    bool success   = x86_enter_user(user_base + header.entry - lowest_address,
                                    user_stack_top, user_api_address,
                                    (ui64)(uip)program.pml4);
    active_program = 0;
    destroy_address_space(&program);
    return success;
}
