/*
 * fat32.c
 *
 * Created on: 2025-06-19
 * Author: debin
 *
 * Description: Simplified FAT32 file system implementation
 */

#include "virtio/fat32.h"
#include "virtio/virtio_blk.h"
#include "tiny_io.h"

static fat32_fs_t fat32_fs;
static uint8_t sector_buf[512];
static uint8_t cluster_buf[512 * 8]; // Assume max 8 sectors per cluster

// Safe unaligned access helper functions
static inline uint16_t read_unaligned_u16(const void *ptr)
{
    const uint8_t *bytes = (const uint8_t *)ptr;
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static inline uint32_t read_unaligned_u32(const void *ptr)
{
    const uint8_t *bytes = (const uint8_t *)ptr;
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline void write_unaligned_u16(void *ptr, uint16_t value)
{
    uint8_t *bytes = (uint8_t *)ptr;
    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)((value >> 8) & 0xFF);
}

static inline void write_unaligned_u32(void *ptr, uint32_t value)
{
    uint8_t *bytes = (uint8_t *)ptr;
    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)((value >> 8) & 0xFF);
    bytes[2] = (uint8_t)((value >> 16) & 0xFF);
    bytes[3] = (uint8_t)((value >> 24) & 0xFF);
}

// Simple string comparison for 8.3 filenames
bool fat32_compare_filename(const char *name1, const char *name2, int len)
{
    for (int i = 0; i < len; i++)
    {
        char c1 = name1[i];
        char c2 = name2[i];

        // Convert to uppercase for comparison
        if (c1 >= 'a' && c1 <= 'z')
            c1 = c1 - 'a' + 'A';
        if (c2 >= 'a' && c2 <= 'z')
            c2 = c2 - 'a' + 'A';

        if (c1 != c2)
            return false;
    }
    return true;
}

// Convert filename to FAT32 8.3 format
void fat32_format_filename(const char *filename, char *fat_name)
{
    int i, j;

    // Clear the fat_name buffer
    for (i = 0; i < 11; i++)
    {
        fat_name[i] = ' ';
    }

    // Copy name part (up to 8 characters)
    for (i = 0, j = 0; i < 8 && filename[j] && filename[j] != '.'; i++, j++)
    {
        char c = filename[j];
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A'; // Convert to uppercase
        fat_name[i] = c;
    }

    // Skip to extension part
    while (filename[j] && filename[j] != '.')
        j++;
    if (filename[j] == '.')
        j++;

    // Copy extension part (up to 3 characters)
    for (i = 8; i < 11 && filename[j]; i++, j++)
    {
        char c = filename[j];
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A'; // Convert to uppercase
        fat_name[i] = c;
    }
}

bool fat32_init(void)
{
    tiny_log(TRACE, "[FAT32] Initializing FAT32 file system\n");

    fat32_fs.initialized = false;

    tiny_log(DEBUG, "[FAT32] Reading boot sector\n");

    if (!fat32_parse_boot_sector())
    {
        tiny_log(WARN, "[FAT32] Boot sector parsing FAILED\n");
        return false;
    }

    fat32_fs.initialized = true;
    tiny_log(TRACE, "[FAT32] File system initialization SUCCESSFUL\n");
    return true;
}

bool fat32_parse_boot_sector(void)
{
    tiny_log(DEBUG, "[FAT32] Parsing boot sector\n");

    // Read boot sector (sector 0)
    if (!virtio_blk_read_sector(0, &fat32_fs.boot_sector))
    {
        tiny_log(WARN, "[FAT32] Failed to read boot sector\n");
        return false;
    }

    // Verify FAT32 signature
    if (read_unaligned_u16(&fat32_fs.boot_sector.bytes_per_sector) != 512)
    {
        tiny_log(WARN, "[FAT32] Invalid bytes per sector: %d\n", read_unaligned_u16(&fat32_fs.boot_sector.bytes_per_sector));
        return false;
    }

    if (read_unaligned_u32(&fat32_fs.boot_sector.fat_size_32) == 0)
    {
        tiny_log(WARN, "[FAT32] Invalid FAT32 signature\n");
        return false;
    }

    // Calculate important values
    fat32_fs.fat_start_sector = read_unaligned_u16(&fat32_fs.boot_sector.reserved_sectors);
    fat32_fs.data_start_sector = fat32_fs.fat_start_sector +
                                 (fat32_fs.boot_sector.num_fats * read_unaligned_u32(&fat32_fs.boot_sector.fat_size_32));
    fat32_fs.root_dir_cluster = read_unaligned_u32(&fat32_fs.boot_sector.root_cluster);
    fat32_fs.sectors_per_cluster = fat32_fs.boot_sector.sectors_per_cluster;
    fat32_fs.bytes_per_sector = read_unaligned_u16(&fat32_fs.boot_sector.bytes_per_sector);

    tiny_log(TRACE, "[FAT32] Boot sector parsed - FAT start: %d, Data start: %d, Root cluster: %d\n",
             fat32_fs.fat_start_sector, fat32_fs.data_start_sector, fat32_fs.root_dir_cluster);
    tiny_log(TRACE, "[FAT32] Sectors per cluster: %d, Bytes per sector: %d\n",
             fat32_fs.sectors_per_cluster, fat32_fs.bytes_per_sector);

    return true;
}

uint32_t fat32_get_next_cluster(uint32_t cluster)
{
    // Calculate FAT entry location
    uint32_t fat_offset = cluster * 4; // 4 bytes per FAT32 entry
    uint32_t fat_sector = fat32_fs.fat_start_sector + (fat_offset / fat32_fs.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fat32_fs.bytes_per_sector;

    tiny_log(DEBUG, "[FAT32] Getting next cluster for %d: FAT sector=%d, offset=%d\n",
             cluster, fat_sector, entry_offset);

    // Read FAT sector
    if (!virtio_blk_read_sector(fat_sector, sector_buf))
    {
        tiny_log(WARN, "[FAT32] Failed to read FAT sector %d\n", fat_sector);
        return FAT32_EOC;
    }

    // Extract next cluster value
    uint32_t next_cluster = *(uint32_t *)(sector_buf + entry_offset) & 0x0FFFFFFF;

    tiny_log(DEBUG, "[FAT32] Next cluster: 0x%x\n", next_cluster);

    return next_cluster;
}

bool fat32_read_cluster(uint32_t cluster, void *buffer)
{
    if (cluster < 2 || cluster >= FAT32_EOC)
    {
        tiny_log(WARN, "[FAT32] Invalid cluster number: %d\n", cluster);
        return false;
    }

    // Calculate cluster's first sector
    uint32_t first_sector = fat32_fs.data_start_sector + ((cluster - 2) * fat32_fs.sectors_per_cluster);

    tiny_log(DEBUG, "[FAT32] Reading cluster %d (sector %d)\n", cluster, first_sector);

    // Read all sectors in the cluster
    for (uint32_t i = 0; i < fat32_fs.sectors_per_cluster; i++)
    {
        uint8_t *buf_offset = (uint8_t *)buffer + (i * fat32_fs.bytes_per_sector);
        if (!virtio_blk_read_sector(first_sector + i, buf_offset))
        {
            tiny_log(WARN, "[FAT32] Failed to read sector %d of cluster %d\n", first_sector + i, cluster);
            return false;
        }
    }

    return true;
}

bool fat32_write_cluster(uint32_t cluster, const void *buffer)
{
    if (cluster < 2 || cluster >= FAT32_EOC)
    {
        tiny_log(WARN, "[FAT32] Invalid cluster number: %d\n", cluster);
        return false;
    }

    // Calculate cluster's first sector
    uint32_t first_sector = fat32_fs.data_start_sector + ((cluster - 2) * fat32_fs.sectors_per_cluster);

    tiny_log(DEBUG, "[FAT32] Writing cluster %d (sector %d)\n", cluster, first_sector);

    // Write all sectors in the cluster
    for (uint32_t i = 0; i < fat32_fs.sectors_per_cluster; i++)
    {
        const uint8_t *buf_offset = (const uint8_t *)buffer + (i * fat32_fs.bytes_per_sector);
        if (!virtio_blk_write_sector(first_sector + i, buf_offset))
        {
            tiny_log(WARN, "[FAT32] Failed to write sector %d of cluster %d\n", first_sector + i, cluster);
            return false;
        }
    }

    return true;
}

bool fat32_find_file_in_dir(uint32_t dir_cluster, const char *filename, fat32_dir_entry_t *entry)
{
    char target_name[11];
    fat32_format_filename(filename, target_name);

    tiny_log(DEBUG, "[FAT32] Looking for file '%s' (formatted as '%s') in cluster %d\n",
             filename, target_name, dir_cluster);

    uint32_t current_cluster = dir_cluster;

    while (current_cluster < FAT32_EOC)
    {
        // Read cluster
        if (!fat32_read_cluster(current_cluster, cluster_buf))
        {
            tiny_log(WARN, "[FAT32] Failed to read directory cluster %d\n", current_cluster);
            return false;
        }

        // Scan directory entries
        uint32_t entries_per_cluster = (fat32_fs.sectors_per_cluster * fat32_fs.bytes_per_sector) / sizeof(fat32_dir_entry_t);
        fat32_dir_entry_t *dir_entries = (fat32_dir_entry_t *)cluster_buf;

        for (uint32_t i = 0; i < entries_per_cluster; i++)
        {
            fat32_dir_entry_t *dir_entry = &dir_entries[i];

            // End of directory
            if (dir_entry->name[0] == 0x00)
            {
                tiny_log(DEBUG, "[FAT32] End of directory reached\n");
                return false;
            }

            // Skip deleted entries and long name entries
            if (dir_entry->name[0] == 0xE5 || dir_entry->attr == FAT_ATTR_LONG_NAME)
            {
                continue;
            }

            // Skip volume labels and directories
            if (dir_entry->attr & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY))
            {
                continue;
            }

            // Compare filename
            tiny_log(DEBUG, "[FAT32] Comparing '%s' with '%s'\n", dir_entry->name, target_name);
            if (fat32_compare_filename((char *)dir_entry->name, target_name, 11))
            {
                tiny_log(TRACE, "[FAT32] File found: '%s', size=%d, first_cluster=%d\n",
                         dir_entry->name, read_unaligned_u32(&dir_entry->file_size),
                         (read_unaligned_u16(&dir_entry->first_cluster_high) << 16) | read_unaligned_u16(&dir_entry->first_cluster_low));

                // Copy entry
                for (int j = 0; j < sizeof(fat32_dir_entry_t); j++)
                {
                    ((uint8_t *)entry)[j] = ((uint8_t *)dir_entry)[j];
                }
                return true;
            }
        }

        // Get next cluster in directory chain
        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    tiny_log(WARN, "[FAT32] File '%s' not found\n", filename);
    return false;
}

bool fat32_read_file(const char *filename, char *buffer, uint32_t max_size)
{
    tiny_log(TRACE, "[FAT32] Reading file '%s'\n", filename);

    if (!fat32_fs.initialized)
    {
        tiny_log(WARN, "[FAT32] File system not initialized\n");
        return false;
    }

    // Find file in root directory
    fat32_dir_entry_t file_entry;
    if (!fat32_find_file_in_dir(fat32_fs.root_dir_cluster, filename, &file_entry))
    {
        tiny_log(WARN, "[FAT32] File '%s' not found\n", filename);
        return false;
    }

    uint32_t file_size = file_entry.file_size;
    uint32_t first_cluster = (file_entry.first_cluster_high << 16) | file_entry.first_cluster_low;

    tiny_log(TRACE, "[FAT32] File info - Size: %d bytes, First cluster: %d\n", file_size, first_cluster);

    if (file_size > max_size)
    {
        tiny_log(WARN, "[FAT32] File too large: %d > %d\n", file_size, max_size);
        return false;
    }

    // Read file data
    uint32_t bytes_read = 0;
    uint32_t current_cluster = first_cluster;
    uint32_t cluster_size = fat32_fs.sectors_per_cluster * fat32_fs.bytes_per_sector;

    while (current_cluster < FAT32_EOC && bytes_read < file_size)
    {
        // Read cluster
        if (!fat32_read_cluster(current_cluster, cluster_buf))
        {
            tiny_log(WARN, "[FAT32] Failed to read file cluster %d\n", current_cluster);
            return false;
        }

        // Copy data to output buffer
        uint32_t bytes_to_copy = (file_size - bytes_read < cluster_size) ? (file_size - bytes_read) : cluster_size;

        for (uint32_t i = 0; i < bytes_to_copy; i++)
        {
            buffer[bytes_read + i] = cluster_buf[i];
        }

        bytes_read += bytes_to_copy;

        tiny_log(DEBUG, "[FAT32] Read cluster %d, bytes_read=%d/%d\n",
                 current_cluster, bytes_read, file_size);

        // Get next cluster
        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    // Null-terminate the buffer
    if (bytes_read < max_size)
    {
        buffer[bytes_read] = '\0';
    }

    tiny_log(TRACE, "[FAT32] File read SUCCESSFUL - %d bytes\n", bytes_read);
    return true;
}

uint32_t fat32_allocate_cluster(void)
{
    tiny_log(DEBUG, "[FAT32] Allocating new cluster\n");

    // Simple allocation: start from cluster 2 and find first free cluster
    // In a real implementation, you'd use the FSInfo sector to track free clusters
    for (uint32_t cluster = 2; cluster < 0x0FFFFFF0; cluster++)
    {
        uint32_t next_cluster = fat32_get_next_cluster(cluster);
        if (next_cluster == FAT32_FREE_CLUSTER)
        {
            tiny_log(DEBUG, "[FAT32] Found free cluster: %d\n", cluster);
            return cluster;
        }
    }

    tiny_log(WARN, "[FAT32] No free clusters available\n");
    return FAT32_EOC;
}

bool fat32_set_fat_entry(uint32_t cluster, uint32_t value)
{
    // Calculate FAT entry location
    uint32_t fat_offset = cluster * 4; // 4 bytes per FAT32 entry
    uint32_t fat_sector = fat32_fs.fat_start_sector + (fat_offset / fat32_fs.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fat32_fs.bytes_per_sector;

    tiny_log(DEBUG, "[FAT32] Setting FAT entry for cluster %d to 0x%x: FAT sector=%d,",
             cluster, value, fat_sector);
    tiny_log(DEBUG, " offset=%d\n", entry_offset);

    // Read FAT sector
    if (!virtio_blk_read_sector(fat_sector, sector_buf))
    {
        tiny_log(WARN, "[FAT32] Failed to read FAT sector %d\n", fat_sector);
        return false;
    }

    // Update FAT entry (preserve upper 4 bits)
    uint32_t *fat_entry = (uint32_t *)(sector_buf + entry_offset);
    uint32_t old_value = *fat_entry;
    *fat_entry = (old_value & 0xF0000000) | (value & 0x0FFFFFFF);

    tiny_log(DEBUG, "[FAT32] Updated FAT entry: 0x%x -> 0x%x\n", old_value, *fat_entry);

    // Write FAT sector back
    if (!virtio_blk_write_sector(fat_sector, sector_buf))
    {
        tiny_log(WARN, "[FAT32] Failed to write FAT sector %d\n", fat_sector);
        return false;
    }

    // Update backup FAT if it exists
    if (fat32_fs.boot_sector.num_fats > 1)
    {
        uint32_t backup_fat_sector = fat_sector + read_unaligned_u32(&fat32_fs.boot_sector.fat_size_32);
        if (!virtio_blk_write_sector(backup_fat_sector, sector_buf))
        {
            tiny_log(WARN, "[FAT32] Failed to write backup FAT sector %d\n", backup_fat_sector);
            // Continue anyway - backup FAT is not critical
        }
    }

    return true;
}

bool fat32_create_dir_entry(uint32_t dir_cluster, const char *filename, uint32_t first_cluster, uint32_t file_size)
{
    char target_name[11];
    fat32_format_filename(filename, target_name);

    tiny_log(DEBUG, "[FAT32] Creating directory entry for '%s' (formatted as '%s') in cluster %d\n",
             filename, target_name, dir_cluster);

    uint32_t current_cluster = dir_cluster;

    while (current_cluster < FAT32_EOC)
    {
        // Read cluster
        if (!fat32_read_cluster(current_cluster, cluster_buf))
        {
            tiny_log(WARN, "[FAT32] Failed to read directory cluster %d\n", current_cluster);
            return false;
        }

        // Scan directory entries to find empty slot
        uint32_t entries_per_cluster = (fat32_fs.sectors_per_cluster * fat32_fs.bytes_per_sector) / sizeof(fat32_dir_entry_t);
        fat32_dir_entry_t *dir_entries = (fat32_dir_entry_t *)cluster_buf;

        for (uint32_t i = 0; i < entries_per_cluster; i++)
        {
            fat32_dir_entry_t *dir_entry = &dir_entries[i];

            // Found empty slot (deleted entry or end of directory)
            if (dir_entry->name[0] == 0x00 || dir_entry->name[0] == 0xE5)
            {
                tiny_log(DEBUG, "[FAT32] Found empty slot at entry %d\n", i);

                // Clear the entry
                for (int j = 0; j < sizeof(fat32_dir_entry_t); j++)
                {
                    ((uint8_t *)dir_entry)[j] = 0;
                }

                // Set filename
                for (int j = 0; j < 11; j++)
                {
                    dir_entry->name[j] = target_name[j];
                }

                // Set attributes
                dir_entry->attr = FAT_ATTR_ARCHIVE;

                // Set cluster and size
                write_unaligned_u16(&dir_entry->first_cluster_low, first_cluster & 0xFFFF);
                write_unaligned_u16(&dir_entry->first_cluster_high, (first_cluster >> 16) & 0xFFFF);
                write_unaligned_u32(&dir_entry->file_size, file_size);

                // Set timestamps (simplified - use current time)
                // For now, just set to some fixed values
                write_unaligned_u16(&dir_entry->create_time, 0x0000);
                write_unaligned_u16(&dir_entry->create_date, 0x0000);
                write_unaligned_u16(&dir_entry->write_time, 0x0000);
                write_unaligned_u16(&dir_entry->write_date, 0x0000);
                write_unaligned_u16(&dir_entry->last_access_date, 0x0000);

                tiny_log(DEBUG, "[FAT32] Directory entry created - cluster=%d, size=%d\n", first_cluster, file_size);

                // Write cluster back
                if (!fat32_write_cluster(current_cluster, cluster_buf))
                {
                    tiny_log(WARN, "[FAT32] Failed to write directory cluster %d\n", current_cluster);
                    return false;
                }

                tiny_log(TRACE, "[FAT32] Directory entry created successfully\n");
                return true;
            }
        }

        // Get next cluster in directory chain
        current_cluster = fat32_get_next_cluster(current_cluster);
    }

    tiny_log(WARN, "[FAT32] No empty directory entry slots found\n");
    return false;
}

bool fat32_write_file(const char *filename, const char *data, uint32_t size)
{
    tiny_log(TRACE, "[FAT32] Writing file '%s' (%d bytes)\n", filename, size);

    if (!fat32_fs.initialized)
    {
        tiny_log(WARN, "[FAT32] File system not initialized\n");
        return false;
    }

    // Check if file already exists
    fat32_dir_entry_t existing_entry;
    if (fat32_find_file_in_dir(fat32_fs.root_dir_cluster, filename, &existing_entry))
    {
        tiny_log(WARN, "[FAT32] File '%s' already exists - overwriting not implemented\n", filename);
        return false;
    }

    // Calculate number of clusters needed
    uint32_t cluster_size = fat32_fs.sectors_per_cluster * fat32_fs.bytes_per_sector;
    uint32_t clusters_needed = (size + cluster_size - 1) / cluster_size;
    if (clusters_needed == 0)
        clusters_needed = 1; // At least one cluster

    tiny_log(DEBUG, "[FAT32] Need %d clusters for %d bytes (cluster size: %d)\n",
             clusters_needed, size, cluster_size);

    // Allocate clusters
    uint32_t first_cluster = fat32_allocate_cluster();
    if (first_cluster >= FAT32_EOC)
    {
        tiny_log(WARN, "[FAT32] Failed to allocate first cluster\n");
        return false;
    }

    uint32_t current_cluster = first_cluster;
    uint32_t bytes_written = 0;

    // Write data to clusters
    for (uint32_t i = 0; i < clusters_needed; i++)
    {
        // Clear cluster buffer
        for (uint32_t j = 0; j < cluster_size; j++)
        {
            cluster_buf[j] = 0;
        }

        // Copy data to cluster buffer
        uint32_t bytes_to_copy = (size - bytes_written < cluster_size) ? (size - bytes_written) : cluster_size;
        for (uint32_t j = 0; j < bytes_to_copy; j++)
        {
            cluster_buf[j] = data[bytes_written + j];
        }

        tiny_log(DEBUG, "[FAT32] Writing cluster %d (%d bytes)\n", current_cluster, bytes_to_copy);

        // Write cluster
        if (!fat32_write_cluster(current_cluster, cluster_buf))
        {
            tiny_log(WARN, "[FAT32] Failed to write cluster %d\n", current_cluster);
            return false;
        }

        bytes_written += bytes_to_copy;

        // Mark cluster as used in FAT
        uint32_t next_cluster_value = FAT32_EOC; // End of chain by default

        // If we need more clusters, allocate next one
        if (i < clusters_needed - 1)
        {
            uint32_t next_cluster = fat32_allocate_cluster();
            if (next_cluster >= FAT32_EOC)
            {
                tiny_log(WARN, "[FAT32] Failed to allocate cluster %d\n", i + 1);
                return false;
            }
            next_cluster_value = next_cluster;
            current_cluster = next_cluster;
        }

        // Update FAT entry
        if (!fat32_set_fat_entry(current_cluster, next_cluster_value))
        {
            tiny_log(WARN, "[FAT32] Failed to update FAT entry for cluster %d\n", current_cluster);
            return false;
        }
    }

    // Create directory entry
    if (!fat32_create_dir_entry(fat32_fs.root_dir_cluster, filename, first_cluster, size))
    {
        tiny_log(WARN, "[FAT32] Failed to create directory entry\n");
        return false;
    }

    tiny_log(TRACE, "[FAT32] File '%s' written successfully (%d bytes)\n", filename, size);
    return true;
}
