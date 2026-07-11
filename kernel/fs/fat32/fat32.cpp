#include <fs/fat32.h>

static fat32_filesystem_t primary_filesystem;

static ui16 read_le16(const ui8 *buffer) {
    return (ui16) buffer[0] | ((ui16) buffer[1] << 8);
}

static ui32 read_le32(const ui8 *buffer) {
    return (ui32) buffer[0] | ((ui32) buffer[1] << 8) | ((ui32) buffer[2] << 16) |
           ((ui32) buffer[3] << 24);
}

static bool is_power_of_two(ui8 value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool fat32_mount(fat32_filesystem_t *filesystem, fat32_read_sector_fn read_sector,
                 void *read_context, ui32 partition_lba,
                 fat32_write_sector_fn write_sector, void *write_context) {
    if (filesystem == 0 || read_sector == 0) return false;
    filesystem->read_sector = 0;
    filesystem->read_context = 0;
    filesystem->write_sector = 0;
    filesystem->write_context = 0;
    filesystem->partition_lba = 0;
    filesystem->fat_start_lba = 0;
    filesystem->data_start_lba = 0;
    filesystem->sectors_per_fat = 0;
    filesystem->fat_count = 0;
    filesystem->root_cluster = 0;
    filesystem->cluster_count = 0;
    filesystem->sectors_per_cluster = 0;
    filesystem->mounted = false;

    ui8 boot_sector[fat32_sector_size];
    if (!read_sector(read_context, partition_lba, boot_sector)) return false;
    if (read_le16(boot_sector + 510) != 0xaa55 || read_le16(boot_sector + 11) != fat32_sector_size) {
        return false;
    }

    ui8 sectors_per_cluster = boot_sector[13];
    ui16 reserved_sector_count = read_le16(boot_sector + 14);
    ui8 fat_count = boot_sector[16];
    ui16 root_entry_count = read_le16(boot_sector + 17);
    ui16 fat16_sectors = read_le16(boot_sector + 22);
    ui32 total_sectors = read_le16(boot_sector + 19);
    ui32 sectors_per_fat = read_le32(boot_sector + 36);
    ui32 root_cluster = read_le32(boot_sector + 44);
    if (total_sectors == 0) total_sectors = read_le32(boot_sector + 32);

    if (!is_power_of_two(sectors_per_cluster) || sectors_per_cluster > 128 ||
        reserved_sector_count == 0 || fat_count == 0 || root_entry_count != 0 || fat16_sectors != 0 ||
        total_sectors == 0 || sectors_per_fat == 0 || root_cluster < 2) {
        return false;
    }

    ui64 metadata_sectors = (ui64) reserved_sector_count + (ui64) fat_count * sectors_per_fat;
    if (metadata_sectors >= total_sectors) return false;

    ui32 data_sectors = total_sectors - (ui32) metadata_sectors;
    ui32 cluster_count = data_sectors / sectors_per_cluster;
    ui64 fat_start_lba = (ui64) partition_lba + reserved_sector_count;
    ui64 data_start_lba = fat_start_lba + (ui64) fat_count * sectors_per_fat;
    if (cluster_count == 0 || root_cluster > cluster_count + 1 || fat_start_lba > 0xffffffffULL ||
        data_start_lba > 0xffffffffULL) {
        return false;
    }

    filesystem->read_sector = read_sector;
    filesystem->read_context = read_context;
    filesystem->write_sector = write_sector;
    filesystem->write_context = write_context;
    filesystem->partition_lba = partition_lba;
    filesystem->fat_start_lba = (ui32) fat_start_lba;
    filesystem->data_start_lba = (ui32) data_start_lba;
    filesystem->sectors_per_fat = sectors_per_fat;
    filesystem->fat_count = fat_count;
    filesystem->root_cluster = root_cluster;
    filesystem->cluster_count = cluster_count;
    filesystem->sectors_per_cluster = sectors_per_cluster;
    filesystem->mounted = true;
    return true;
}

static constexpr ui16 ata_data_port = 0x1f0;
static constexpr ui16 ata_sector_count_port = 0x1f2;
static constexpr ui16 ata_lba_low_port = 0x1f3;
static constexpr ui16 ata_lba_mid_port = 0x1f4;
static constexpr ui16 ata_lba_high_port = 0x1f5;
static constexpr ui16 ata_drive_port = 0x1f6;
static constexpr ui16 ata_status_port = 0x1f7;
static constexpr ui16 ata_command_port = 0x1f7;
static constexpr ui16 ata_alt_status_port = 0x3f6;

static ui8 ata_in8(ui16 port) {
    ui8 value;
    asm ("inb %1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

static ui16 ata_in16(ui16 port) {
    ui16 value;
    asm ("inw %1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

static void ata_out8(ui16 port, ui8 value) {
    asm ("outb %0, %1" : : "a" (value), "Nd" (port));
}

static bool ata_wait_ready() {
    for (ui32 index = 0; index < 1000000; index++) {
        ui8 status = ata_in8(ata_status_port);
        if ((status & 0x01) != 0 || (status & 0x20) != 0) return false;
        if ((status & 0x80) == 0) return true;
    }
    return false;
}

static bool ata_wait_data() {
    for (ui32 index = 0; index < 1000000; index++) {
        ui8 status = ata_in8(ata_status_port);
        if ((status & 0x01) != 0 || (status & 0x20) != 0) return false;
        if ((status & 0x80) == 0 && (status & 0x08) != 0) return true;
    }
    return false;
}

static bool ata_read_sector(void *context, ui32 lba, ui8 *buffer) {
    (void) context;
    if (buffer == 0 || lba > 0x0fffffff || !ata_wait_ready()) return false;

    ata_out8(ata_drive_port, (ui8) (0xe0 | ((lba >> 24) & 0x0f)));
    ata_in8(ata_alt_status_port);
    ata_in8(ata_alt_status_port);
    ata_in8(ata_alt_status_port);
    ata_in8(ata_alt_status_port);

    ata_out8(ata_sector_count_port, 1);
    ata_out8(ata_lba_low_port, (ui8) lba);
    ata_out8(ata_lba_mid_port, (ui8) (lba >> 8));
    ata_out8(ata_lba_high_port, (ui8) (lba >> 16));
    ata_out8(ata_command_port, 0x20);
    if (!ata_wait_data()) return false;

    for (ui32 index = 0; index < fat32_sector_size / sizeof(ui16); index++) {
        ui16 value = ata_in16(ata_data_port);
        buffer[index * 2] = (ui8) value;
        buffer[index * 2 + 1] = (ui8) (value >> 8);
    }
    return true;
}

static bool ata_write_sector(void *context, ui32 lba, const ui8 *buffer) {
    (void) context;
    if (buffer == 0 || lba > 0x0fffffff || !ata_wait_ready()) return false;

    ata_out8(ata_drive_port, (ui8) (0xe0 | ((lba >> 24) & 0x0f)));
    ata_in8(ata_alt_status_port);
    ata_in8(ata_alt_status_port);
    ata_in8(ata_alt_status_port);
    ata_in8(ata_alt_status_port);

    ata_out8(ata_sector_count_port, 1);
    ata_out8(ata_lba_low_port, (ui8) lba);
    ata_out8(ata_lba_mid_port, (ui8) (lba >> 8));
    ata_out8(ata_lba_high_port, (ui8) (lba >> 16));
    ata_out8(ata_command_port, 0x30);
    if (!ata_wait_data()) return false;

    for (ui32 index = 0; index < fat32_sector_size / sizeof(ui16); index++) {
        ui16 value = (ui16) buffer[index * 2] | ((ui16) buffer[index * 2 + 1] << 8);
        asm ("outw %0, %1" : : "a" (value), "Nd" (ata_data_port));
    }

    ata_out8(ata_command_port, 0xe7);
    return ata_wait_ready();
}

bool fat32_mount_primary_ata(fat32_filesystem_t *filesystem) {
    if (filesystem == 0) return false;

    ui8 mbr[fat32_sector_size];
    if (!ata_read_sector(0, 0, mbr) || read_le16(mbr + 510) != 0xaa55) return false;

    for (ui32 index = 0; index < 4; index++) {
        const ui8 *partition = mbr + 446 + index * 16;
        if (partition[4] != 0x0b && partition[4] != 0x0c) continue;

        ui32 partition_lba = read_le32(partition + 8);
        if (partition_lba == 0) return false;
        return fat32_mount(filesystem, ata_read_sector, 0, partition_lba, ata_write_sector, 0);
    }

    return false;
}

bool fat32_mount_primary_ata() {
    return fat32_mount_primary_ata(&primary_filesystem);
}

bool fat32_open(const char *path, fat32_file_t *file) {
    return fat32_open(&primary_filesystem, path, file);
}

bool fat32_directory_exists(const char *path) {
    return fat32_directory_exists(&primary_filesystem, path);
}

fat32_directory_entry_t *fat32_list_directory(const char *path) {
    return fat32_list_directory(&primary_filesystem, path);
}

bool fat32_create_file(const char *path) {
    return fat32_create_file(&primary_filesystem, path);
}

bool fat32_create_directory(const char *path) {
    return fat32_create_directory(&primary_filesystem, path);
}

i64 fat32_write(const char *path, ui32 offset, const ui8 *buffer, ui32 size) {
    return fat32_write(&primary_filesystem, path, offset, buffer, size);
}

bool fat32_rename(const char *path, const char *new_path) {
    return fat32_rename(&primary_filesystem, path, new_path);
}

bool fat32_remove(const char *path) {
    return fat32_remove(&primary_filesystem, path);
}
