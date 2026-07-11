#include <program.h>
#include <console.h>
#include <fs/fat32.h>
#include <scheduler.h>

namespace {

constexpr ui8 elf_class_64 = 2;
constexpr ui8 elf_data_little_endian = 1;
constexpr ui8 elf_version_current = 1;
constexpr ui16 elf_type_dynamic = 3;
constexpr ui16 elf_machine_x86_64 = 62;
constexpr ui32 elf_program_load = 1;
constexpr ui32 elf_program_dynamic = 2;
constexpr ui32 elf_program_interpreter = 3;
constexpr ui32 elf_program_tls = 7;
constexpr ui32 elf_segment_executable = 1;
constexpr ui64 elf_dynamic_needed = 1;
constexpr ui64 elf_dynamic_rela = 7;
constexpr ui64 elf_dynamic_rel = 17;
constexpr ui64 elf_dynamic_jmprel = 23;
constexpr ui64 page_size = 4096;
constexpr ui64 max_program_size = 16 * 1024 * 1024;
constexpr ui16 max_program_headers = 64;

struct elf64_header_t {
    ui8 ident[16];
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
} __attribute__ ((packed));

struct elf64_program_header_t {
    ui32 type;
    ui32 flags;
    ui64 offset;
    ui64 virtual_address;
    ui64 physical_address;
    ui64 file_size;
    ui64 memory_size;
    ui64 alignment;
} __attribute__ ((packed));

struct elf64_dynamic_t {
    ui64 tag;
    ui64 value;
} __attribute__ ((packed));

bool range_fits(ui64 offset, ui64 size, ui64 limit) {
    return offset <= limit && size <= limit - offset;
}

bool read_exact(const fat32_file_t &file, ui64 offset, void *buffer, ui64 size) {
    if (!range_fits(offset, size, file.size) || size > 0xffffffffu) return false;
    return fat32_read(&file, (ui32) offset, (ui8 *) buffer, (ui32) size) == (i64) size;
}

bool read_program_header(const fat32_file_t &file, const elf64_header_t &header,
                         ui16 index, elf64_program_header_t *program_header) {
    ui64 offset = header.program_header_offset + (ui64) index * sizeof(*program_header);
    return read_exact(file, offset, program_header, sizeof(*program_header));
}

bool dynamic_segment_supported(const fat32_file_t &file,
                               const elf64_program_header_t &program_header) {
    if (!range_fits(program_header.offset, program_header.file_size, file.size) ||
        program_header.file_size % sizeof(elf64_dynamic_t) != 0) return false;

    ui64 count = program_header.file_size / sizeof(elf64_dynamic_t);
    for (ui64 index = 0; index < count; index++) {
        elf64_dynamic_t dynamic;
        if (!read_exact(file, program_header.offset + index * sizeof(dynamic),
                        &dynamic, sizeof(dynamic))) return false;
        if (dynamic.tag == elf_dynamic_needed || dynamic.tag == elf_dynamic_rela ||
            dynamic.tag == elf_dynamic_rel || dynamic.tag == elf_dynamic_jmprel) return false;
    }
    return true;
}

}

bool program_run(const char *path) {
    fat32_file_t file;
    elf64_header_t header;
    if (path == 0 || !fat32_open(path, &file) || !read_exact(file, 0, &header, sizeof(header))) return false;

    if (header.ident[0] != 0x7f || header.ident[1] != 'E' || header.ident[2] != 'L' ||
        header.ident[3] != 'F' || header.ident[4] != elf_class_64 ||
        header.ident[5] != elf_data_little_endian || header.ident[6] != elf_version_current ||
        header.type != elf_type_dynamic || header.machine != elf_machine_x86_64 ||
        header.version != elf_version_current || header.header_size != sizeof(header) ||
        header.program_header_size != sizeof(elf64_program_header_t) ||
        header.program_header_count == 0 || header.program_header_count > max_program_headers ||
        !range_fits(header.program_header_offset,
                    (ui64) header.program_header_count * sizeof(elf64_program_header_t), file.size)) return false;

    ui64 lowest_address = (ui64) -1;
    ui64 highest_address = 0;
    bool entry_is_executable = false;
    for (ui16 index = 0; index < header.program_header_count; index++) {
        elf64_program_header_t program_header;
        if (!read_program_header(file, header, index, &program_header)) return false;
        if (program_header.type == elf_program_interpreter || program_header.type == elf_program_tls) return false;
        if (program_header.type == elf_program_dynamic &&
            !dynamic_segment_supported(file, program_header)) return false;
        if (program_header.type != elf_program_load) continue;

        if (program_header.file_size > program_header.memory_size ||
            !range_fits(program_header.offset, program_header.file_size, file.size) ||
            (program_header.alignment != 0 &&
             ((program_header.alignment & (program_header.alignment - 1)) != 0 ||
              ((program_header.virtual_address - program_header.offset) &
               (program_header.alignment - 1)) != 0)) ||
            program_header.virtual_address > (ui64) -1 - program_header.memory_size) return false;

        ui64 segment_start = program_header.virtual_address & ~(page_size - 1);
        ui64 segment_end_unaligned = program_header.virtual_address + program_header.memory_size;
        if (segment_end_unaligned > (ui64) -1 - (page_size - 1)) return false;
        ui64 segment_end = (segment_end_unaligned + page_size - 1) & ~(page_size - 1);
        if (segment_start < lowest_address) lowest_address = segment_start;
        if (segment_end > highest_address) highest_address = segment_end;
        if ((program_header.flags & elf_segment_executable) != 0 &&
            header.entry >= program_header.virtual_address &&
            header.entry < program_header.virtual_address + program_header.memory_size) {
            entry_is_executable = true;
        }
    }

    if (!entry_is_executable || lowest_address == (ui64) -1 || highest_address <= lowest_address ||
        highest_address - lowest_address > max_program_size) return false;

    ui64 image_size = highest_address - lowest_address;
    ui8 *allocation = new ui8[image_size + page_size - 1];
    ui8 *image = (ui8 *) (((uip) allocation + page_size - 1) & ~(uip) (page_size - 1));
    for (ui64 index = 0; index < image_size; index++) image[index] = 0;

    for (ui16 index = 0; index < header.program_header_count; index++) {
        elf64_program_header_t program_header;
        if (!read_program_header(file, header, index, &program_header) ||
            (program_header.type == elf_program_load &&
             !read_exact(file, program_header.offset,
                         image + program_header.virtual_address - lowest_address,
                         program_header.file_size))) {
            delete[] allocation;
            return false;
        }
    }

    static const program_api_t api = { write_console, scheduler_yield };
    typedef void (*program_entry_t) (const program_api_t *api);
    program_entry_t entry = (program_entry_t) (image + header.entry - lowest_address);
    entry(&api);
    delete[] allocation;
    return true;
}
