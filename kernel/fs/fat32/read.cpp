#include <fs/fat32/_read.h>

static ui16 read_le16(const ui8 *buffer) {
    return (ui16) buffer[0] | ((ui16) buffer[1] << 8);
}

static ui32 read_le32(const ui8 *buffer) {
    return (ui32) buffer[0] | ((ui32) buffer[1] << 8) | ((ui32) buffer[2] << 16) |
           ((ui32) buffer[3] << 24);
}

static bool fat32_cluster_valid(const fat32_filesystem_t *filesystem, ui32 cluster) {
    return cluster >= 2 && cluster <= filesystem->cluster_count + 1;
}

static bool fat32_cluster_end(ui32 cluster) {
    return cluster >= 0x0ffffff8 && cluster <= 0x0fffffff;
}

static bool fat32_cluster_lba(const fat32_filesystem_t *filesystem, ui32 cluster, ui32 *lba) {
    if (!fat32_cluster_valid(filesystem, cluster)) return false;

    ui64 result = (ui64) filesystem->data_start_lba +
                  (ui64) (cluster - 2) * filesystem->sectors_per_cluster;
    if (result > 0xffffffffULL) return false;

    *lba = (ui32) result;
    return true;
}

static ui32 fat32_next_cluster(const fat32_filesystem_t *filesystem, ui32 cluster) {
    if (!fat32_cluster_valid(filesystem, cluster)) return 0;

    ui32 fat_sector = cluster / (fat32_sector_size / sizeof(ui32));
    if (fat_sector >= filesystem->sectors_per_fat) return 0;

    ui8 sector[fat32_sector_size];
    if (!filesystem->read_sector(filesystem->read_context, filesystem->fat_start_lba + fat_sector,
                                 sector)) {
        return 0;
    }

    ui32 offset = (cluster % (fat32_sector_size / sizeof(ui32))) * sizeof(ui32);
    return read_le32(sector + offset) & 0x0fffffff;
}

static bool fat32_make_name(const char *start, ui32 length, ui8 name[11]) {
    for (ui32 index = 0; index < 11; index++) name[index] = ' ';

    ui32 dot = length;
    for (ui32 index = 0; index < length; index++) {
        if (start[index] == '.') {
            if (dot != length) return false;
            dot = index;
        }
    }

    ui32 base_length = dot;
    ui32 extension_length = dot == length ? 0 : length - dot - 1;
    if (base_length == 0 || base_length > 8 || extension_length > 3 ||
        (dot != length && extension_length == 0)) {
        return false;
    }

    for (ui32 index = 0; index < base_length; index++) {
        char character = start[index];
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        name[index] = (ui8) character;
    }

    for (ui32 index = 0; index < extension_length; index++) {
        char character = start[dot + 1 + index];
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        name[8 + index] = (ui8) character;
    }

    return true;
}

static bool fat32_names_match(const ui8 *entry, const ui8 name[11]) {
    for (ui32 index = 0; index < 11; index++) {
        if (entry[index] != name[index]) return false;
    }
    return true;
}

static bool fat32_directory_entry_is_dot(const ui8 *entry) {
    return entry[0] == '.' &&
           (entry[1] == ' ' || (entry[1] == '.' && entry[2] == ' '));
}

static void fat32_directory_entry_name(const ui8 *entry, char name[13]) {
    for (ui32 index = 0; index < 13; index++) name[index] = 0;

    ui32 base_length = 8;
    while (base_length != 0 && entry[base_length - 1] == ' ') base_length--;
    for (ui32 index = 0; index < base_length; index++) {
        name[index] = (char) (index == 0 && entry[index] == 0x05 ? 0xe5 : entry[index]);
    }

    ui32 extension_length = 3;
    while (extension_length != 0 && entry[8 + extension_length - 1] == ' ') extension_length--;
    if (extension_length == 0) return;

    name[base_length] = '.';
    for (ui32 index = 0; index < extension_length; index++) {
        name[base_length + 1 + index] = (char) entry[8 + index];
    }
}

static bool fat32_find_in_directory(const fat32_filesystem_t *filesystem, ui32 directory_cluster,
                                    const ui8 name[11], ui8 entry[32]) {
    ui32 cluster = directory_cluster;

    for (ui32 cluster_index = 0; cluster_index < filesystem->cluster_count; cluster_index++) {
        ui32 cluster_lba;
        if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba)) return false;

        for (ui32 sector_index = 0; sector_index < filesystem->sectors_per_cluster; sector_index++) {
            ui8 sector[fat32_sector_size];
            if (!filesystem->read_sector(filesystem->read_context, cluster_lba + sector_index,
                                         sector)) {
                return false;
            }

            for (ui32 offset = 0; offset < fat32_sector_size; offset += 32) {
                const ui8 *directory_entry = sector + offset;
                if (directory_entry[0] == 0x00) return false;
                if (directory_entry[0] == 0xe5 || directory_entry[11] == 0x0f ||
                    (directory_entry[11] & 0x08) != 0) {
                    continue;
                }

                if (fat32_names_match(directory_entry, name)) {
                    for (ui32 byte_index = 0; byte_index < 32; byte_index++) {
                        entry[byte_index] = directory_entry[byte_index];
                    }
                    return true;
                }
            }
        }

        ui32 next_cluster = fat32_next_cluster(filesystem, cluster);
        if (fat32_cluster_end(next_cluster)) return false;
        if (!fat32_cluster_valid(filesystem, next_cluster)) return false;
        cluster = next_cluster;
    }

    return false;
}

bool fat32_open(const fat32_filesystem_t *filesystem, const char *path, fat32_file_t *file) {
    if (filesystem == 0 || path == 0 || file == 0 || !filesystem->mounted) return false;

    const char *cursor = path;
    while (*cursor == '/') cursor++;
    if (*cursor == 0) return false;

    ui32 directory_cluster = filesystem->root_cluster;
    while (true) {
        const char *segment_start = cursor;
        while (*cursor != 0 && *cursor != '/') cursor++;
        ui32 segment_length = (ui32) (cursor - segment_start);

        ui8 name[11];
        if (!fat32_make_name(segment_start, segment_length, name)) return false;

        bool final_segment = *cursor == 0;
        while (*cursor == '/') cursor++;
        if (!final_segment && *cursor == 0) return false;

        ui8 entry[32];
        if (!fat32_find_in_directory(filesystem, directory_cluster, name, entry)) return false;

        bool is_directory = (entry[11] & 0x10) != 0;
        ui32 first_cluster = ((ui32) read_le16(entry + 20) << 16) | read_le16(entry + 26);
        if (final_segment) {
            if (is_directory) return false;
            file->filesystem = filesystem;
            file->first_cluster = first_cluster;
            file->size = read_le32(entry + 28);
            return file->size == 0 || fat32_cluster_valid(filesystem, first_cluster);
        }

        if (!is_directory || !fat32_cluster_valid(filesystem, first_cluster)) return false;
        directory_cluster = first_cluster;
    }
}

i64 fat32_read(const fat32_file_t *file, ui32 offset, ui8 *buffer, ui32 size) {
    if (file == 0 || buffer == 0 || file->filesystem == 0 || !file->filesystem->mounted) {
        return fat32_read_error;
    }
    if (offset >= file->size || size == 0) return 0;

    const fat32_filesystem_t *filesystem = file->filesystem;
    ui32 remaining = file->size - offset;
    if (size < remaining) remaining = size;

    ui32 cluster_size = (ui32) filesystem->sectors_per_cluster * fat32_sector_size;
    ui32 cluster = file->first_cluster;
    ui32 clusters_to_skip = offset / cluster_size;
    ui32 cluster_offset = offset % cluster_size;

    for (ui32 index = 0; index < clusters_to_skip; index++) {
        cluster = fat32_next_cluster(filesystem, cluster);
        if (!fat32_cluster_valid(filesystem, cluster)) return fat32_read_error;
    }

    ui32 bytes_read = 0;
    while (remaining != 0) {
        ui32 cluster_lba;
        if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba)) return fat32_read_error;

        ui32 sector_index = cluster_offset / fat32_sector_size;
        ui32 sector_offset = cluster_offset % fat32_sector_size;
        while (sector_index < filesystem->sectors_per_cluster && remaining != 0) {
            ui8 sector[fat32_sector_size];
            if (!filesystem->read_sector(filesystem->read_context, cluster_lba + sector_index,
                                         sector)) {
                return fat32_read_error;
            }

            ui32 copy_size = fat32_sector_size - sector_offset;
            if (copy_size > remaining) copy_size = remaining;
            for (ui32 index = 0; index < copy_size; index++) {
                buffer[bytes_read + index] = sector[sector_offset + index];
            }

            bytes_read += copy_size;
            remaining -= copy_size;
            sector_index++;
            sector_offset = 0;
        }

        cluster_offset = 0;
        if (remaining != 0) {
            cluster = fat32_next_cluster(filesystem, cluster);
            if (!fat32_cluster_valid(filesystem, cluster)) return fat32_read_error;
        }
    }

    return (i64) bytes_read;
}

fat32_directory_entry_t *fat32_list_directory(const fat32_filesystem_t *filesystem,
                                              const char *path) {
    if (filesystem == 0 || path == 0 || !filesystem->mounted) return 0;

    const char *cursor = path;
    while (*cursor == '/') cursor++;

    ui32 directory_cluster = filesystem->root_cluster;
    while (*cursor != 0) {
        const char *segment_start = cursor;
        while (*cursor != 0 && *cursor != '/') cursor++;
        ui32 segment_length = (ui32) (cursor - segment_start);

        ui8 name[11];
        if (!fat32_make_name(segment_start, segment_length, name)) return 0;

        while (*cursor == '/') cursor++;

        ui8 entry[32];
        if (!fat32_find_in_directory(filesystem, directory_cluster, name, entry) ||
            (entry[11] & 0x10) == 0) {
            return 0;
        }

        ui32 first_cluster = ((ui32) read_le16(entry + 20) << 16) | read_le16(entry + 26);
        if (!fat32_cluster_valid(filesystem, first_cluster)) return 0;
        directory_cluster = first_cluster;
    }

    fat32_directory_entry_t *head = 0;
    fat32_directory_entry_t *tail = 0;
    ui32 cluster = directory_cluster;
    for (ui32 cluster_index = 0; cluster_index < filesystem->cluster_count; cluster_index++) {
        ui32 cluster_lba;
        if (!fat32_cluster_lba(filesystem, cluster, &cluster_lba)) break;

        for (ui32 sector_index = 0; sector_index < filesystem->sectors_per_cluster; sector_index++) {
            ui8 sector[fat32_sector_size];
            if (!filesystem->read_sector(filesystem->read_context, cluster_lba + sector_index,
                                         sector)) {
                fat32_free_directory_list(head);
                return 0;
            }

            for (ui32 offset = 0; offset < fat32_sector_size; offset += 32) {
                const ui8 *entry = sector + offset;
                if (entry[0] == 0x00) return head;
                if (entry[0] == 0xe5 || entry[11] == 0x0f || (entry[11] & 0x08) != 0 ||
                    fat32_directory_entry_is_dot(entry)) {
                    continue;
                }

                fat32_directory_entry_t *node = new fat32_directory_entry_t;
                fat32_directory_entry_name(entry, node->name);
                node->attributes = entry[11];
                node->first_cluster = ((ui32) read_le16(entry + 20) << 16) | read_le16(entry + 26);
                node->size = read_le32(entry + 28);
                node->next = 0;

                if (tail == 0) {
                    head = node;
                } else {
                    tail->next = node;
                }
                tail = node;
            }
        }

        ui32 next_cluster = fat32_next_cluster(filesystem, cluster);
        if (fat32_cluster_end(next_cluster)) return head;
        if (!fat32_cluster_valid(filesystem, next_cluster)) break;
        cluster = next_cluster;
    }

    fat32_free_directory_list(head);
    return 0;
}

void fat32_free_directory_list(fat32_directory_entry_t *entries) {
    while (entries != 0) {
        fat32_directory_entry_t *next = entries->next;
        delete entries;
        entries = next;
    }
}
