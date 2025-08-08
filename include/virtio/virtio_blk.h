#ifndef __VIRTIO_BLK_H__
#define __VIRTIO_BLK_H__

#include "virtio_mmio.h"

// VirtIO Block device feature bits
#define VIRTIO_BLK_F_SIZE_MAX 1
#define VIRTIO_BLK_F_SEG_MAX 2
#define VIRTIO_BLK_F_GEOMETRY 4
#define VIRTIO_BLK_F_RO 5
#define VIRTIO_BLK_F_BLK_SIZE 6
#define VIRTIO_BLK_F_FLUSH 9
#define VIRTIO_BLK_F_TOPOLOGY 10
#define VIRTIO_BLK_F_CONFIG_WCE 11

// VirtIO Block request types
#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_FLUSH 4

// VirtIO Block request status
#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2

// VirtIO Block configuration space
typedef struct
{
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    struct
    {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    } geometry;
    uint32_t blk_size;
    struct
    {
        uint8_t physical_block_exp;
        uint8_t alignment_offset;
        uint16_t min_io_size;
        uint32_t opt_io_size;
    } topology;
    uint8_t writeback;
} __attribute__((packed)) virtio_blk_config_t;

// VirtIO Block request header
typedef struct
{
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed, aligned(8))) virtio_blk_req_header_t;

// VirtIO Block request structure
typedef struct
{
    virtio_blk_req_header_t header;
    uint8_t status;
    uint8_t padding[15]; // 确保16字节对齐
} __attribute__((aligned(16))) virtio_blk_req_t;

// Sector size
#define VIRTIO_BLK_SECTOR_SIZE 512

// Function declarations
bool virtio_blk_init(void);
bool virtio_blk_read_sector(uint32_t sector, void *buffer);
bool virtio_blk_write_sector(uint32_t sector, const void *buffer);
uint64_t virtio_blk_get_capacity(void);
bool virtio_blk_test(void);

// Performance testing functions
bool virtio_blk_performance_test(void);

#endif // __VIRTIO_BLK_H__
