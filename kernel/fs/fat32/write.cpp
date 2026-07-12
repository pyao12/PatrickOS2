#include <fs/fat32/_write.h>

namespace {

constexpr ui32 fat32_end_of_chain        = 0x0fffffff;
constexpr ui8  fat32_attribute_directory = 0x10;
constexpr ui8  fat32_attribute_archive   = 0x20;
constexpr ui8  fat32_attribute_long_name = 0x0f;

struct fat32_entry_location_t {
    ui32 cluster;
    ui32 sector_lba;
    ui32 offset;
    ui8  entry[32];
};

static ui16 read_le16(const ui8 *buffer) { return (ui16)buffer[0] | ((ui16)buffer[1] << 8); }

static ui32 read_le32(const ui8 *buffer) {
    return (ui32)buffer[0] | ((ui32)buffer[1] << 8) | ((ui32)buffer[2] << 16) | ((ui32)buffer[3] << 24);
}

static void write_le16(ui8 *buffer, ui16 value) {
    buffer[0] = (ui8)value;
    buffer[1] = (ui8)(value >> 8);
}

static void write_le32(ui8 *buffer, ui32 value) {
    buffer[0] = (ui8)value;
    buffer[1] = (ui8)(value >> 8);
    buffer[2] = (ui8)(value >> 16);
    buffer[3] = (ui8)(value >> 24);
}

static void copy_bytes(ui8 *dst, const ui8 *src, ui32 size) {
    for (ui32 index = 0; index < size; index++)
        dst[index] = src[index];
}

static void set_bytes(ui8 *dst, ui8 value, ui32 size) {
    for (ui32 index = 0; index < size; index++)
        dst[index] = value;
}

static bool fat32_cluster_valid(const fat32_filesystem_t *filesystem, ui32 cluster) {
    return cluster >= 2 && cluster <= filesystem->cluster_count + 1;
}

static bool fat32_cluster_end(ui32 cluster) { return cluster >= 0x0ffffff8 && cluster <= 0x0fffffff; }

static bool fat32_cluster_lba(const fat32_filesystem_t *filesystem, ui32 cluster, ui32 *lba) {
    if (!fat32_cluster_valid(filesystem, cluster)) {
        ;
        return false;
    }

    ui64 result = (ui64)filesystem->data_start_lba + (ui64)(cluster - 2) * filesystem->sectors_per_cluster;
    if (result > 0xffffffffULL) {
        ;
        return false;
    }

    *lba = (ui32)result;
    return true;
}

static bool fat32_mount_writable(const fat32_filesystem_t *filesystem) {
    return filesystem != 0 && filesystem->mounted && filesystem->read_sector != 0 && filesystem->write_sector != 0;
}

static bool fat32_read_sector(const fat32_filesystem_t *filesystem, ui32 lba, ui8 *buffer) {
    return filesystem->read_sector(filesystem->read_context, lba, buffer);
}

static bool fat32_write_sector(const fat32_filesystem_t *filesystem, ui32 lba, const ui8 *buffer) {
    return filesystem->write_sector(filesystem->write_context, lba, buffer);
}

static bool fat32_read_fat_entry(const fat32_filesystem_t *filesystem, ui32 cluster, ui32 *value) {
    if (!fat32_cluster_valid(filesystem, cluster) || value == 0) {
        ;
        return false;
    }

    ui32 entries_per_sector = fat32_sector_size / sizeof(ui32);
    ui32 fat_sector         = cluster / entries_per_sector;
    if (fat_sector >= filesystem->sectors_per_fat) {
        ;
        return false;
    }

    ui8 sector[fat32_sector_size];
    if (!fat32_read_sector(filesystem, filesystem->fat_start_lba + fat_sector, sector)) {
        ;
        return false;
    }

    ui32 offset = (cluster % entries_per_sector) * sizeof(ui32);
    *value      = read_le32(sector + offset) & 0x0fffffff;
    return true;
}

static ui32 fat32_next_cluster(const fat32_filesystem_t *filesystem, ui32 cluster) {
    ui32 value = 0;
    if (!fat32_read_fat_entry(filesystem, cluster, &value))
        return 0;
    return value;
}

static bool fat32_write_fat_entry(const fat32_filesystem_t *filesystem, ui32 cluster, ui32 value) {
    if (!fat32_cluster_valid(filesystem, cluster)) {
        ;
        return false;
    }

    ui32 entries_per_sector = fat32_sector_size / sizeof(ui32);
    ui32 fat_sector         = cluster / entries_per_sector;
    if (fat_sector >= filesystem->sectors_per_fat) {
        ;
        return false;
    }

    ui32 offset = (cluster % entries_per_sector) * sizeof(ui32);
    for (ui32 fat_index = 0; fat_index < filesystem->fat_count; fat_index++) {
        ui32 lba = filesystem->fat_start_lba + fat_index * filesystem->sectors_per_fat + fat_sector;
        ui8  sector[fat32_sector_size];
        if (!fat32_read_sector(filesystem, lba, sector)) {
            ;
            return false;
        }
        ui32 existing = read_le32(sector + offset) & 0xf0000000;
        write_le32(sector + offset, existing | (value & 0x0fffffff));
        if (!fat32_write_sector(filesystem, lba, sector)) {
            ;
            return false;
        }
    }

    return true;
}

static bool fat32_zero_cluster(const fat32_filesystem_t *filesystem, ui32 cluster) {
    ui32 cluster_lba;
    if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba))
        return false;

    ui8 sector[fat32_sector_size];
    set_bytes(sector, 0, sizeof(sector));
    for (ui32 sector_index = 0; sector_index < filesystem->sectors_per_cluster; sector_index++) {
        if (!fat32_write_sector(filesystem, cluster_lba + sector_index, sector)) {
            ;
            return false;
        }
    }
    return true;
}

static bool fat32_allocate_cluster(const fat32_filesystem_t *filesystem, ui32 *cluster_out) {
    if (cluster_out == 0) {
        ;
        return false;
    }

    for (ui32 cluster = 2; cluster <= filesystem->cluster_count + 1; cluster++) {
        ui32 value = 0;
        if (!fat32_read_fat_entry(filesystem, cluster, &value))
            return false;
        if (value != 0)
            continue;
        if (!fat32_write_fat_entry(filesystem, cluster, fat32_end_of_chain) || !fat32_zero_cluster(filesystem, cluster)) {
            return false;
        }
        *cluster_out = cluster;
        return true;
    }

    ;
    return false;
}

static bool fat32_free_cluster_chain(const fat32_filesystem_t *filesystem, ui32 first_cluster) {
    ui32 cluster = first_cluster;
    while (fat32_cluster_valid(filesystem, cluster)) {
        ui32 next_cluster = fat32_next_cluster(filesystem, cluster);
        if (!fat32_write_fat_entry(filesystem, cluster, 0))
            return false;
        if (fat32_cluster_end(next_cluster))
            return true;
        if (!fat32_cluster_valid(filesystem, next_cluster)) {
            ;
            return false;
        }
        cluster = next_cluster;
    }
    return first_cluster == 0;
}

static bool fat32_make_name(const char *start, ui32 length, ui8 name[11]) {
    for (ui32 index = 0; index < 11; index++)
        name[index] = ' ';

    ui32 dot = length;
    for (ui32 index = 0; index < length; index++) {
        char character = start[index];
        if (character == '.') {
            if (dot != length)
                return false;
            dot = index;
            continue;
        }
        if (character < 0x21 || character == '"' || character == '*' || character == '+' || character == ',' ||
            character == '/' || character == ':' || character == ';' || character == '<' || character == '=' ||
            character == '>' || character == '?' || character == '[' || character == '\\' || character == ']' ||
            character == '|') {
            return false;
        }
    }

    ui32 base_length      = dot;
    ui32 extension_length = dot == length ? 0 : length - dot - 1;
    if (base_length == 0 || base_length > 8 || extension_length > 3 || (dot != length && extension_length == 0) ||
        (base_length == 1 && start[0] == '.') || (base_length == 2 && start[0] == '.' && start[1] == '.')) {
        return false;
    }

    for (ui32 index = 0; index < base_length; index++) {
        char character = start[index];
        if (character >= 'a' && character <= 'z')
            character -= 'a' - 'A';
        name[index] = (ui8)character;
    }

    for (ui32 index = 0; index < extension_length; index++) {
        char character = start[dot + 1 + index];
        if (character >= 'a' && character <= 'z')
            character -= 'a' - 'A';
        name[8 + index] = (ui8)character;
    }

    return true;
}

static bool fat32_names_match(const ui8 *entry, const ui8 name[11]) {
    for (ui32 index = 0; index < 11; index++) {
        if (entry[index] != name[index])
            return false;
    }
    return true;
}

static bool fat32_directory_entry_is_dot(const ui8 *entry) {
    return entry[0] == '.' && (entry[1] == ' ' || (entry[1] == '.' && entry[2] == ' '));
}

static ui32 fat32_entry_first_cluster(const ui8 *entry) { return ((ui32)read_le16(entry + 20) << 16) | read_le16(entry + 26); }

static void fat32_entry_set_first_cluster(ui8 *entry, ui32 cluster) {
    write_le16(entry + 20, (ui16)(cluster >> 16));
    write_le16(entry + 26, (ui16)cluster);
}

static void fat32_build_entry(ui8 *entry, const ui8 name[11], ui8 attributes, ui32 first_cluster, ui32 size) {
    set_bytes(entry, 0, 32);
    copy_bytes(entry, name, 11);
    entry[11] = attributes;
    fat32_entry_set_first_cluster(entry, first_cluster);
    write_le32(entry + 28, size);
}

static bool fat32_write_entry_at(const fat32_filesystem_t *filesystem, const fat32_entry_location_t &location,
                                 const ui8 entry[32]) {
    ui8 sector[fat32_sector_size];
    if (!fat32_read_sector(filesystem, location.sector_lba, sector)) {
        ;
        return false;
    }
    copy_bytes(sector + location.offset, entry, 32);
    if (!fat32_write_sector(filesystem, location.sector_lba, sector)) {
        ;
        return false;
    }
    return true;
}

static bool fat32_find_in_directory(const fat32_filesystem_t *filesystem, ui32 directory_cluster, const ui8 name[11],
                                    fat32_entry_location_t *location) {
    ui32 cluster = directory_cluster;

    for (ui32 cluster_index = 0; cluster_index < filesystem->cluster_count; cluster_index++) {
        ui32 cluster_lba;
        if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba))
            return false;

        for (ui32 sector_index = 0; sector_index < filesystem->sectors_per_cluster; sector_index++) {
            ui8  sector[fat32_sector_size];
            ui32 sector_lba = cluster_lba + sector_index;
            if (!fat32_read_sector(filesystem, sector_lba, sector)) {
                ;
                return false;
            }

            for (ui32 offset = 0; offset < fat32_sector_size; offset += 32) {
                const ui8 *directory_entry = sector + offset;
                if (directory_entry[0] == 0x00)
                    return false;
                if (directory_entry[0] == 0xe5 || directory_entry[11] == fat32_attribute_long_name ||
                    (directory_entry[11] & 0x08) != 0) {
                    continue;
                }

                if (fat32_names_match(directory_entry, name)) {
                    if (location != 0) {
                        location->cluster    = cluster;
                        location->sector_lba = sector_lba;
                        location->offset     = offset;
                        copy_bytes(location->entry, directory_entry, 32);
                    }
                    return true;
                }
            }
        }

        ui32 next_cluster = fat32_next_cluster(filesystem, cluster);
        if (fat32_cluster_end(next_cluster))
            return false;
        if (!fat32_cluster_valid(filesystem, next_cluster)) {
            ;
            return false;
        }
        cluster = next_cluster;
    }

    return false;
}

static bool fat32_find_free_directory_slot(const fat32_filesystem_t *filesystem, ui32 directory_cluster,
                                           fat32_entry_location_t *location) {
    if (location == 0) {
        ;
        return false;
    }

    ui32 cluster  = directory_cluster;
    ui32 previous = 0;
    while (true) {
        ui32 cluster_lba;
        if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba))
            return false;

        for (ui32 sector_index = 0; sector_index < filesystem->sectors_per_cluster; sector_index++) {
            ui8  sector[fat32_sector_size];
            ui32 sector_lba = cluster_lba + sector_index;
            if (!fat32_read_sector(filesystem, sector_lba, sector)) {
                ;
                return false;
            }

            for (ui32 offset = 0; offset < fat32_sector_size; offset += 32) {
                if (sector[offset] != 0x00 && sector[offset] != 0xe5)
                    continue;
                location->cluster    = cluster;
                location->sector_lba = sector_lba;
                location->offset     = offset;
                set_bytes(location->entry, 0, sizeof(location->entry));
                return true;
            }
        }

        ui32 next_cluster = fat32_next_cluster(filesystem, cluster);
        if (fat32_cluster_end(next_cluster)) {
            ui32 new_cluster = 0;
            if (!fat32_allocate_cluster(filesystem, &new_cluster) || !fat32_write_fat_entry(filesystem, cluster, new_cluster) ||
                !fat32_write_fat_entry(filesystem, new_cluster, fat32_end_of_chain) ||
                !fat32_cluster_lba(filesystem, new_cluster, &location->sector_lba)) {
                ;
                return false;
            }
            location->cluster = new_cluster;
            location->offset  = 0;
            set_bytes(location->entry, 0, sizeof(location->entry));
            (void)previous;
            return true;
        }
        if (!fat32_cluster_valid(filesystem, next_cluster)) {
            ;
            return false;
        }
        previous = cluster;
        cluster  = next_cluster;
    }
}

static bool fat32_resolve_parent(const fat32_filesystem_t *filesystem, const char *path, ui32 *parent_cluster, ui8 name[11],
                                 fat32_entry_location_t *existing_location, bool *exists) {
    if (!fat32_mount_writable(filesystem) || path == 0 || parent_cluster == 0 || name == 0 || exists == 0) {
        ;
        return false;
    }

    const char *cursor = path;
    while (*cursor == '/')
        cursor++;
    if (*cursor == 0) {
        ;
        return false;
    }

    ui32 directory_cluster = filesystem->root_cluster;
    while (true) {
        const char *segment_start = cursor;
        while (*cursor != 0 && *cursor != '/')
            cursor++;
        ui32 segment_length = (ui32)(cursor - segment_start);
        if (segment_length == 0) {
            ;
            return false;
        }

        ui8 segment_name[11];
        if (!fat32_make_name(segment_start, segment_length, segment_name)) {
            ;
            return false;
        }

        bool final_segment = *cursor == 0;
        while (*cursor == '/')
            cursor++;
        if (!final_segment && *cursor == 0) {
            ;
            return false;
        }

        if (final_segment) {
            *parent_cluster = directory_cluster;
            copy_bytes(name, segment_name, 11);
            *exists = fat32_find_in_directory(filesystem, directory_cluster, segment_name, existing_location);
            return true;
        }

        fat32_entry_location_t child;
        if (!fat32_find_in_directory(filesystem, directory_cluster, segment_name, &child)) {
            ;
            return false;
        }
        if ((child.entry[11] & fat32_attribute_directory) == 0) {
            ;
            return false;
        }

        ui32 child_cluster = fat32_entry_first_cluster(child.entry);
        if (!fat32_cluster_valid(filesystem, child_cluster)) {
            ;
            return false;
        }
        directory_cluster = child_cluster;
    }
}

static bool fat32_update_entry_size_and_cluster(const fat32_filesystem_t *filesystem, fat32_entry_location_t *location,
                                                ui32 first_cluster, ui32 size) {
    if (location == 0) {
        ;
        return false;
    }
    fat32_entry_set_first_cluster(location->entry, first_cluster);
    write_le32(location->entry + 28, size);
    return fat32_write_entry_at(filesystem, *location, location->entry);
}

static bool fat32_directory_parent_cluster(const fat32_filesystem_t *filesystem, ui32 cluster, ui32 *parent_cluster) {
    if (parent_cluster == 0) {
        ;
        return false;
    }
    if (cluster == filesystem->root_cluster) {
        *parent_cluster = filesystem->root_cluster;
        return true;
    }

    ui32 cluster_lba;
    if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba))
        return false;

    ui8 sector[fat32_sector_size];
    if (!fat32_read_sector(filesystem, cluster_lba, sector)) {
        ;
        return false;
    }
    const ui8 *entry = sector + 32;
    if (entry[0] != '.' || entry[1] != '.') {
        ;
        return false;
    }

    ui32 result     = fat32_entry_first_cluster(entry);
    *parent_cluster = result == 0 ? filesystem->root_cluster : result;
    return true;
}

static bool fat32_directory_is_descendant(const fat32_filesystem_t *filesystem, ui32 directory_cluster, ui32 ancestor_cluster) {
    ui32 cluster = directory_cluster;
    for (ui32 depth = 0; depth < filesystem->cluster_count; depth++) {
        if (cluster == ancestor_cluster)
            return true;
        if (cluster == filesystem->root_cluster)
            return false;
        if (!fat32_directory_parent_cluster(filesystem, cluster, &cluster))
            return false;
    }
    return false;
}

static bool fat32_directory_empty(const fat32_filesystem_t *filesystem, ui32 cluster) {
    for (ui32 cluster_index = 0; cluster_index < filesystem->cluster_count; cluster_index++) {
        ui32 cluster_lba;
        if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba))
            return false;

        for (ui32 sector_index = 0; sector_index < filesystem->sectors_per_cluster; sector_index++) {
            ui8 sector[fat32_sector_size];
            if (!fat32_read_sector(filesystem, cluster_lba + sector_index, sector)) {
                ;
                return false;
            }
            for (ui32 offset = 0; offset < fat32_sector_size; offset += 32) {
                const ui8 *entry = sector + offset;
                if (entry[0] == 0x00)
                    return true;
                if (entry[0] == 0xe5 || entry[11] == fat32_attribute_long_name || (entry[11] & 0x08) != 0 ||
                    fat32_directory_entry_is_dot(entry)) {
                    continue;
                }
                return false;
            }
        }

        ui32 next_cluster = fat32_next_cluster(filesystem, cluster);
        if (fat32_cluster_end(next_cluster))
            return true;
        if (!fat32_cluster_valid(filesystem, next_cluster)) {
            ;
            return false;
        }
        cluster = next_cluster;
    }

    return false;
}

static bool fat32_nth_cluster(const fat32_filesystem_t *filesystem, ui32 first_cluster, ui32 index, ui32 *cluster_out) {
    if (cluster_out == 0) {
        ;
        return false;
    }
    ui32 cluster = first_cluster;
    for (ui32 current = 0; current < index; current++) {
        cluster = fat32_next_cluster(filesystem, cluster);
        if (!fat32_cluster_valid(filesystem, cluster)) {
            ;
            return false;
        }
    }
    *cluster_out = cluster;
    return true;
}

static bool fat32_chain_length(const fat32_filesystem_t *filesystem, ui32 first_cluster, ui32 *length, ui32 *last_cluster) {
    if (length == 0 || last_cluster == 0) {
        ;
        return false;
    }
    if (first_cluster == 0) {
        *length       = 0;
        *last_cluster = 0;
        return true;
    }
    if (!fat32_cluster_valid(filesystem, first_cluster)) {
        ;
        return false;
    }

    ui32 cluster = first_cluster;
    ui32 count   = 1;
    for (; count <= filesystem->cluster_count; count++) {
        ui32 next_cluster = fat32_next_cluster(filesystem, cluster);
        if (fat32_cluster_end(next_cluster)) {
            *length       = count;
            *last_cluster = cluster;
            return true;
        }
        if (!fat32_cluster_valid(filesystem, next_cluster)) {
            ;
            return false;
        }
        cluster = next_cluster;
    };
    return false;
}

static bool fat32_resize_chain(const fat32_filesystem_t *filesystem, ui32 *first_cluster, ui32 cluster_count) {
    if (first_cluster == 0) {
        ;
        return false;
    }

    if (cluster_count == 0) {
        bool ok = *first_cluster == 0 || fat32_free_cluster_chain(filesystem, *first_cluster);
        if (ok)
            *first_cluster = 0;
        return ok;
    }

    ui32 current_length = 0;
    ui32 last_cluster   = 0;
    if (!fat32_chain_length(filesystem, *first_cluster, &current_length, &last_cluster))
        return false;

    if (current_length == 0) {
        if (!fat32_allocate_cluster(filesystem, first_cluster))
            return false;
        current_length = 1;
        last_cluster   = *first_cluster;
    }

    while (current_length < cluster_count) {
        ui32 new_cluster = 0;
        if (!fat32_allocate_cluster(filesystem, &new_cluster) ||
            !fat32_write_fat_entry(filesystem, last_cluster, new_cluster) ||
            !fat32_write_fat_entry(filesystem, new_cluster, fat32_end_of_chain)) {
            ;
            return false;
        }
        last_cluster = new_cluster;
        current_length++;
    }

    if (current_length == cluster_count)
        return true;

    ui32 keep_last = 0;
    if (!fat32_nth_cluster(filesystem, *first_cluster, cluster_count - 1, &keep_last))
        return false;

    ui32 tail = fat32_next_cluster(filesystem, keep_last);
    if (!fat32_write_fat_entry(filesystem, keep_last, fat32_end_of_chain)) {
        ;
        return false;
    }
    if (fat32_cluster_valid(filesystem, tail))
        return fat32_free_cluster_chain(filesystem, tail);
    return fat32_cluster_end(tail);
}

static bool fat32_write_range(const fat32_filesystem_t *filesystem, ui32 first_cluster, ui32 offset, const ui8 *buffer,
                              ui32 size, bool zero_fill) {
    if (size == 0)
        return true;
    if (!fat32_cluster_valid(filesystem, first_cluster)) {
        ;
        return false;
    }

    ui32 cluster_size     = (ui32)filesystem->sectors_per_cluster * fat32_sector_size;
    ui32 cluster          = first_cluster;
    ui32 clusters_to_skip = offset / cluster_size;
    ui32 cluster_offset   = offset % cluster_size;

    for (ui32 index = 0; index < clusters_to_skip; index++) {
        cluster = fat32_next_cluster(filesystem, cluster);
        if (!fat32_cluster_valid(filesystem, cluster)) {
            ;
            return false;
        }
    }

    ui32 bytes_written = 0;
    while (bytes_written < size) {
        ui32 cluster_lba;
        if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba))
            return false;

        ui32 sector_index  = cluster_offset / fat32_sector_size;
        ui32 sector_offset = cluster_offset % fat32_sector_size;
        while (sector_index < filesystem->sectors_per_cluster && bytes_written < size) {
            ui8  sector[fat32_sector_size];
            ui32 sector_lba = cluster_lba + sector_index;
            ui32 chunk      = fat32_sector_size - sector_offset;
            if (chunk > size - bytes_written)
                chunk = size - bytes_written;

            if (sector_offset != 0 || chunk != fat32_sector_size) {
                if (!fat32_read_sector(filesystem, sector_lba, sector)) {
                    ;
                    return false;
                }
            } else {
                set_bytes(sector, 0, sizeof(sector));
            }

            if (zero_fill) {
                set_bytes(sector + sector_offset, 0, chunk);
            } else {
                copy_bytes(sector + sector_offset, buffer + bytes_written, chunk);
            }

            if (!fat32_write_sector(filesystem, sector_lba, sector)) {
                ;
                return false;
            }
            bytes_written += chunk;
            sector_index++;
            sector_offset = 0;
        }

        cluster_offset = 0;
        if (bytes_written < size) {
            cluster = fat32_next_cluster(filesystem, cluster);
            if (!fat32_cluster_valid(filesystem, cluster)) {
                ;
                return false;
            }
        }
    }

    return true;
}

static bool fat32_mark_entry_deleted(const fat32_filesystem_t *filesystem, const fat32_entry_location_t &location) {
    ui8 entry[32];
    copy_bytes(entry, location.entry, sizeof(entry));
    entry[0] = 0xe5;
    return fat32_write_entry_at(filesystem, location, entry);
}

static bool fat32_write_dot_entries(const fat32_filesystem_t *filesystem, ui32 cluster, ui32 parent_cluster) {
    ui32 cluster_lba;
    if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba))
        return false;

    ui8 sector[fat32_sector_size];
    if (!fat32_read_sector(filesystem, cluster_lba, sector)) {
        ;
        return false;
    }

    ui8 dot_name[11]    = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    ui8 dotdot_name[11] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    ui8 dot_entry[32];
    ui8 dotdot_entry[32];
    fat32_build_entry(dot_entry, dot_name, fat32_attribute_directory, cluster, 0);
    fat32_build_entry(dotdot_entry, dotdot_name, fat32_attribute_directory,
                      parent_cluster == filesystem->root_cluster ? filesystem->root_cluster : parent_cluster, 0);
    copy_bytes(sector, dot_entry, 32);
    copy_bytes(sector + 32, dotdot_entry, 32);
    return fat32_write_sector(filesystem, cluster_lba, sector);
}

} // namespace

bool fat32_create_file(fat32_filesystem_t *filesystem, const char *path) {
    ui32                   parent_cluster = 0;
    ui8                    name[11];
    fat32_entry_location_t existing;
    bool                   exists = false;
    if (!fat32_resolve_parent(filesystem, path, &parent_cluster, name, &existing, &exists)) {
        return false;
    }
    if (exists) {
        ;
        return false;
    }

    fat32_entry_location_t slot;
    if (!fat32_find_free_directory_slot(filesystem, parent_cluster, &slot)) {
        ;
        return false;
    }

    ui8 entry[32];
    fat32_build_entry(entry, name, fat32_attribute_archive, 0, 0);
    return fat32_write_entry_at(filesystem, slot, entry);
}

bool fat32_create_directory(fat32_filesystem_t *filesystem, const char *path) {
    ui32                   parent_cluster = 0;
    ui8                    name[11];
    fat32_entry_location_t existing;
    bool                   exists = false;
    if (!fat32_resolve_parent(filesystem, path, &parent_cluster, name, &existing, &exists)) {
        return false;
    }
    if (exists) {
        ;
        return false;
    }

    ui32 cluster = 0;
    if (!fat32_allocate_cluster(filesystem, &cluster) || !fat32_write_dot_entries(filesystem, cluster, parent_cluster)) {
        if (cluster != 0)
            fat32_free_cluster_chain(filesystem, cluster);
        ;
        return false;
    }

    fat32_entry_location_t slot;
    if (!fat32_find_free_directory_slot(filesystem, parent_cluster, &slot)) {
        fat32_free_cluster_chain(filesystem, cluster);
        ;
        return false;
    }

    ui8 entry[32];
    fat32_build_entry(entry, name, fat32_attribute_directory, cluster, 0);
    if (!fat32_write_entry_at(filesystem, slot, entry)) {
        fat32_free_cluster_chain(filesystem, cluster);
        ;
        return false;
    }
    return true;
}

i64 fat32_write(fat32_filesystem_t *filesystem, const char *path, ui32 offset, const ui8 *buffer, ui32 size) {
    if (!fat32_mount_writable(filesystem) || path == 0 || (buffer == 0 && size != 0)) {
        ;
        return fat32_write_error;
    }

    ui32                   parent_cluster = 0;
    ui8                    name[11];
    fat32_entry_location_t location;
    bool                   exists = false;
    if (!fat32_resolve_parent(filesystem, path, &parent_cluster, name, &location, &exists) || !exists) {
        return fat32_write_error;
    }
    if ((location.entry[11] & fat32_attribute_directory) != 0) {
        ;
        return fat32_write_error;
    }

    ui32 file_size     = read_le32(location.entry + 28);
    ui32 first_cluster = fat32_entry_first_cluster(location.entry);
    ui64 end_offset    = (ui64)offset + size;
    if (end_offset > 0xffffffffULL) {
        ;
        return fat32_write_error;
    }

    ui32 new_size = file_size;
    if (end_offset > file_size)
        new_size = (ui32)end_offset;

    ui32 cluster_size      = (ui32)filesystem->sectors_per_cluster * fat32_sector_size;
    ui32 required_clusters = new_size == 0 ? 0 : (new_size + cluster_size - 1) / cluster_size;
    if (!fat32_resize_chain(filesystem, &first_cluster, required_clusters)) {
        ;
        return fat32_write_error;
    }

    if (offset > file_size && first_cluster != 0 &&
        !fat32_write_range(filesystem, first_cluster, file_size, 0, offset - file_size, true)) {
        ;
        return fat32_write_error;
    }
    if (size != 0 && !fat32_write_range(filesystem, first_cluster, offset, buffer, size, false)) {
        ;
        return fat32_write_error;
    }

    if (!fat32_update_entry_size_and_cluster(filesystem, &location, first_cluster, new_size)) {
        ;
        return fat32_write_error;
    }
    return size;
}

bool fat32_rename(fat32_filesystem_t *filesystem, const char *path, const char *new_path) {
    ui32                   source_parent_cluster = 0;
    ui8                    source_name[11];
    fat32_entry_location_t source;
    bool                   source_exists = false;
    if (!fat32_resolve_parent(filesystem, path, &source_parent_cluster, source_name, &source, &source_exists)) {
        return false;
    }
    if (!source_exists) {
        ;
        return false;
    }

    ui32                   target_parent_cluster = 0;
    ui8                    target_name[11];
    fat32_entry_location_t target;
    bool                   target_exists = false;
    if (!fat32_resolve_parent(filesystem, new_path, &target_parent_cluster, target_name, &target, &target_exists)) {
        return false;
    }
    if (target_exists) {
        ;
        return false;
    }

    bool source_is_directory = (source.entry[11] & fat32_attribute_directory) != 0;
    ui32 source_cluster      = fat32_entry_first_cluster(source.entry);
    if (source_is_directory && target_parent_cluster != source_parent_cluster &&
        fat32_directory_is_descendant(filesystem, target_parent_cluster, source_cluster)) {
        ;
        return false;
    }

    if (source_parent_cluster == target_parent_cluster) {
        copy_bytes(source.entry, target_name, 11);
        return fat32_write_entry_at(filesystem, source, source.entry);
    }

    fat32_entry_location_t slot;
    if (!fat32_find_free_directory_slot(filesystem, target_parent_cluster, &slot)) {
        ;
        return false;
    }

    ui8 moved_entry[32];
    copy_bytes(moved_entry, source.entry, sizeof(moved_entry));
    copy_bytes(moved_entry, target_name, 11);
    if (!fat32_write_entry_at(filesystem, slot, moved_entry))
        return false;

    if (source_is_directory && source_cluster != 0 &&
        !fat32_write_dot_entries(filesystem, source_cluster, target_parent_cluster)) {
        fat32_mark_entry_deleted(filesystem, slot);
        ;
        return false;
    }

    return fat32_mark_entry_deleted(filesystem, source);
}

bool fat32_remove(fat32_filesystem_t *filesystem, const char *path) {
    ui32                   parent_cluster = 0;
    ui8                    name[11];
    fat32_entry_location_t location;
    bool                   exists = false;
    if (!fat32_resolve_parent(filesystem, path, &parent_cluster, name, &location, &exists)) {
        return false;
    }
    if (!exists) {
        ;
        return false;
    }

    bool is_directory  = (location.entry[11] & fat32_attribute_directory) != 0;
    ui32 first_cluster = fat32_entry_first_cluster(location.entry);
    if (is_directory) {
        if (!fat32_cluster_valid(filesystem, first_cluster) || !fat32_directory_empty(filesystem, first_cluster)) {
            ;
            return false;
        }
    }

    if (first_cluster != 0 && !fat32_free_cluster_chain(filesystem, first_cluster)) {
        ;
        return false;
    }
    return fat32_mark_entry_deleted(filesystem, location);
}
