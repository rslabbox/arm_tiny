/*
 * virtio_mmio.c
 *
 * Created on: 2025-06-19
 * Author: debin
 *
 * Description: VirtIO MMIO device driver implementation
 */

#include "virtio/virtio_mmio.h"
#include "virtio/virtio_blk.h"
#include "virtio/virtio_interrupt.h"
#include "tiny_io.h"
#include "config.h"

static virtio_queue_manager_t queue_manager_global = {0};
static bool queue_manager_initialized = false;
static virtio_device_t virtio_dev;

virtio_queue_manager_t *virtio_get_queue_manager()
{
    return &queue_manager_global;
}

// ARM cache management functions for DMA coherency
void virtio_cache_clean_range(uint64_t start, uint32_t size)
{
    uint64_t end = start + size;
    uint64_t line_size = 64; // ARM cache line size

    tiny_log(DEBUG, "[VIRTIO] Cache clean: addr=0x%x, size=%d\n", (uint32_t)start, size);

    // Align to cache line boundaries
    start = start & ~(line_size - 1);
    end = (end + line_size - 1) & ~(line_size - 1);

    for (uint64_t addr = start; addr < end; addr += line_size)
    {
        __asm__ volatile("dc cvac, %0" : : "r"(addr) : "memory");
    }

    // Data memory barrier
    __asm__ volatile("dmb sy" ::: "memory");
}

void virtio_cache_invalidate_range(uint64_t start, uint32_t size)
{
    uint64_t end = start + size;
    uint64_t line_size = 64; // ARM cache line size

    // tiny_log(DEBUG, "[VIRTIO] Cache invalidate: addr=0x%x, size=%d\n", (uint32_t)start, size);

    // Align to cache line boundaries
    start = start & ~(line_size - 1);
    end = (end + line_size - 1) & ~(line_size - 1);

    for (uint64_t addr = start; addr < end; addr += line_size)
    {
        __asm__ volatile("dc ivac, %0" : : "r"(addr) : "memory");
    }

    // Data memory barrier
    __asm__ volatile("dmb sy" ::: "memory");
}

// Helper function to display VirtIO features
static void virtio_display_features(uint64_t features, const char *prefix)
{
    tiny_log(TRACE, "[VIRTIO] %s features: 0x%x%08x\n", prefix,
             (uint32_t)(features >> 32), (uint32_t)features);

    if (features & (1ULL << VIRTIO_F_VERSION_1))
    {
        tiny_log(DEBUG, "[VIRTIO]   - VERSION_1 (modern mode)\n");
    }
    if (features & (1ULL << VIRTIO_F_ACCESS_PLATFORM))
    {
        tiny_log(DEBUG, "[VIRTIO]   - ACCESS_PLATFORM\n");
    }
    if (features & (1ULL << VIRTIO_F_RING_PACKED))
    {
        tiny_log(DEBUG, "[VIRTIO]   - RING_PACKED\n");
    }
    if (features & (1ULL << VIRTIO_F_IN_ORDER))
    {
        tiny_log(DEBUG, "[VIRTIO]   - IN_ORDER\n");
    }
}

uint32_t virtio_read32(uint64_t addr)
{
    // Check for 4-byte alignment
    if (addr & 0x3)
    {
        tiny_log(ERROR, "[VIRTIO] READ32 ALIGNMENT ERROR: addr=0x%x is not 4-byte aligned!\n", (uint32_t)addr);
        tiny_log(ERROR, "[VIRTIO] Address must be aligned to 4-byte boundary (addr & 0x3 == 0)\n");
        tiny_log(ERROR, "[VIRTIO] Current alignment offset: %d bytes\n", (uint32_t)(addr & 0x3));
    }

    uint32_t value = read32((void *)addr);
    tiny_log(DEBUG, "[VIRTIO] READ32: addr=0x%x, value=0x%x\n", (uint32_t)addr, value);
    return value;
}

void virtio_write32(uint64_t addr, uint32_t value)
{
    // Check for 4-byte alignment
    if (addr & 0x3)
    {
        tiny_log(ERROR, "[VIRTIO] WRITE32 ALIGNMENT ERROR: addr=0x%x is not 4-byte aligned!\n", (uint32_t)addr);
        tiny_log(ERROR, "[VIRTIO] Address must be aligned to 4-byte boundary (addr & 0x3 == 0)\n");
        tiny_log(ERROR, "[VIRTIO] Current alignment offset: %d bytes\n", (uint32_t)(addr & 0x3));
    }

    tiny_log(DEBUG, "[VIRTIO] WRITE32: addr=0x%x, value=0x%x\n", (uint32_t)addr, value);
    write32(value, (void *)addr);
}

bool virtio_probe_device(uint64_t base_addr)
{
    tiny_log(TRACE, "[VIRTIO] Probing device at address 0x%x\n", (uint32_t)base_addr);

    // Read magic number
    uint32_t magic = virtio_read32(base_addr + VIRTIO_MMIO_MAGIC);
    if (magic != VIRTIO_MAGIC_VALUE)
    {
        tiny_log(WARN, "[VIRTIO] Invalid magic number: expected 0x%x, got 0x%x\n",
                 VIRTIO_MAGIC_VALUE, magic);
        return false;
    }

    tiny_log(TRACE, "[VIRTIO] Magic number check PASSED: 0x%x\n", magic);

    // Read version
    uint32_t version = virtio_read32(base_addr + VIRTIO_MMIO_VERSION);
    if (version < 1 || version > 2)
    {
        tiny_log(WARN, "[VIRTIO] Unsupported version: %d (supported: 1-2)\n", version);
        return false;
    }

    tiny_log(TRACE, "[VIRTIO] Version check PASSED: %d\n", version);

    // Read device ID
    uint32_t device_id = virtio_read32(base_addr + VIRTIO_MMIO_DEVICE_ID);
    if (device_id == 0)
    {
        tiny_log(WARN, "[VIRTIO] No device present (device_id = 0)\n");
        return false;
    }

    tiny_log(TRACE, "[VIRTIO] Device ID: %d\n", device_id);

    return true;
}

bool virtio_device_init(virtio_device_t *dev, uint64_t base_addr)
{
    tiny_log(TRACE, "[VIRTIO] Initializing device at 0x%x\n", (uint32_t)base_addr);

    // Probe device first
    if (!virtio_probe_device(base_addr))
    {
        tiny_log(WARN, "[VIRTIO] Device probe FAILED\n");
        return false;
    }

    // Fill device structure
    dev->base_addr = base_addr;
    dev->magic = virtio_read32(base_addr + VIRTIO_MMIO_MAGIC);

    uint32_t hw_version = virtio_read32(base_addr + VIRTIO_MMIO_VERSION);
    dev->device_id = virtio_read32(base_addr + VIRTIO_MMIO_DEVICE_ID);
    dev->vendor_id = virtio_read32(base_addr + VIRTIO_MMIO_VENDOR_ID);

    dev->version = hw_version;

    tiny_log(TRACE, "[VIRTIO] Device info - Magic: 0x%x, Version: %d, Device ID: %d, Vendor ID: 0x%x\n",
             dev->magic, dev->version, dev->device_id, dev->vendor_id);

    // VirtIO version support: Both Legacy (v1) and Modern (v2+) modes
    if (dev->version == 1)
    {
        tiny_log(TRACE, "[VIRTIO] Device uses VirtIO 1.0 Legacy mode\n");
    }
    else if (dev->version >= 2)
    {
        tiny_log(TRACE, "[VIRTIO] Device uses VirtIO 1.1+ Modern mode (version %d)\n", dev->version);
    }
    else
    {
        tiny_log(WARN, "[VIRTIO] Unknown VirtIO version: %d\n", dev->version);
    }

    // Reset device
    virtio_set_status(dev, 0);
    tiny_log(TRACE, "[VIRTIO] Device reset completed\n");

    // Acknowledge the device
    virtio_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE);
    tiny_log(TRACE, "[VIRTIO] Device acknowledged\n");

    // Indicate that we know how to drive the device
    virtio_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    tiny_log(TRACE, "[VIRTIO] Driver status set\n");

    // Read device features - handle both 32-bit and 64-bit feature negotiation
    uint32_t device_features_low, device_features_high = 0;
    uint64_t device_features, driver_features;

    // Read low 32 bits
    virtio_write32(dev->base_addr + VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    device_features_low = virtio_read32(dev->base_addr + VIRTIO_MMIO_DEVICE_FEATURES);

    // Read high 32 bits (for VirtIO 2.0+ features)
    virtio_write32(dev->base_addr + VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    device_features_high = virtio_read32(dev->base_addr + VIRTIO_MMIO_DEVICE_FEATURES);

    device_features = ((uint64_t)device_features_high << 32) | device_features_low;
    virtio_display_features(device_features, "Device");

    // Feature negotiation based on VirtIO version
    if (dev->version >= 2)
    {
        // VirtIO 2.0+ modern mode - selective feature negotiation

        // Ensure VERSION_1 feature is supported for modern mode
        if (!(device_features & (1ULL << VIRTIO_F_VERSION_1)))
        {
            tiny_log(WARN, "[VIRTIO] Device doesn't support VERSION_1 feature for modern mode\n");
            return false;
        }

        // Start with supported common features
        driver_features = device_features & VIRTIO_SUPPORTED_FEATURES_MASK;

        // Add device-specific features we want to support
        // For block device, we can accept basic features
        uint32_t device_specific_mask = 0;
        if (dev->device_id == VIRTIO_DEVICE_ID_BLOCK)
        {
            // Accept basic block device features
            device_specific_mask = (1 << VIRTIO_BLK_F_SIZE_MAX) |
                                   (1 << VIRTIO_BLK_F_SEG_MAX) |
                                   (1 << VIRTIO_BLK_F_BLK_SIZE);
        }

        driver_features |= (device_features_low & device_specific_mask);

        tiny_log(TRACE, "[VIRTIO] Modern mode: VERSION_1 feature confirmed, selective negotiation\n");
        virtio_display_features(driver_features, "Driver");
    }
    else
    {
        // VirtIO 1.0 legacy mode - accept all device features (legacy behavior)
        driver_features = device_features_low;
        tiny_log(TRACE, "[VIRTIO] Legacy mode: using 32-bit features only\n");
    }

    // Write driver features back
    virtio_write32(dev->base_addr + VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    virtio_write32(dev->base_addr + VIRTIO_MMIO_DRIVER_FEATURES, (uint32_t)driver_features);

    if (dev->version >= 2)
    {
        virtio_write32(dev->base_addr + VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        virtio_write32(dev->base_addr + VIRTIO_MMIO_DRIVER_FEATURES, (uint32_t)(driver_features >> 32));
    }

    tiny_log(TRACE, "[VIRTIO] Driver features set to: 0x%x%x\n",
             (uint32_t)(driver_features >> 32), (uint32_t)driver_features);

    // Indicate that feature negotiation is complete
    virtio_set_status(dev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    tiny_log(TRACE, "[VIRTIO] Features OK status set\n");

    // Check if device accepted our features
    uint8_t status = virtio_read32(dev->base_addr + VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK))
    {
        tiny_log(ERROR, "[VIRTIO] Device rejected our features\n");
        return false;
    }

    dev->ready = false;

    tiny_log(TRACE, "[VIRTIO] Device initialization SUCCESSFUL\n");
    return true;
}

void virtio_set_status(virtio_device_t *dev, uint8_t status)
{
    // Read back to verify
    uint8_t current_status = virtio_read32(dev->base_addr + VIRTIO_MMIO_STATUS);
    tiny_log(DEBUG, "[VIRTIO] Status readback: 0x%x\n", current_status);

    tiny_log(DEBUG, "[VIRTIO] Setting device status to 0x%x\n", status);
    virtio_write32(dev->base_addr + VIRTIO_MMIO_STATUS, status);

    // Read back to verify
    current_status = virtio_read32(dev->base_addr + VIRTIO_MMIO_STATUS);
    tiny_log(DEBUG, "[VIRTIO] Status readback: 0x%x\n", current_status);
}

// Multi-queue management functions
bool virtio_queue_manager_init(void)
{
    virtio_queue_manager_t *queue_manager = virtio_get_queue_manager();
    tiny_log(TRACE, "[VIRTIO] Initializing queue manager %d\n", queue_manager_initialized);
    if (queue_manager_initialized)
    {
        tiny_log(DEBUG, "[VIRTIO] Queue manager already initialized\n");
        return true;
    }
    tiny_log(TRACE, "[VIRTIO] Initializing queue manager %d\n", queue_manager_initialized);
    // Initialize all queues as free
    for (uint32_t i = 0; i < VIRTIO_MAX_TOTAL_QUEUES; i++)
    {
        queue_manager->queues[i].in_use = false;
        queue_manager->queues[i].queue_id = 0;
        queue_manager->queues[i].device = NULL;
    }
    tiny_log(TRACE, "[VIRTIO] Initializing queue manager %d\n", queue_manager_initialized);

    queue_manager->next_queue_id = 1; // Start from 1, 0 is invalid
    queue_manager->allocated_count = 0;
    queue_manager_initialized = true;

    tiny_log(TRACE, "[VIRTIO] Queue manager initialized (max queues: %d)\n", VIRTIO_MAX_TOTAL_QUEUES);
    return true;
}

virtqueue_t *virtio_queue_alloc(virtio_device_t *dev, uint32_t device_queue_idx)
{
    virtio_queue_manager_t *queue_manager = virtio_get_queue_manager();
    if (!queue_manager_initialized)
    {
        if (!virtio_queue_manager_init())
        {
            tiny_log(ERROR, "[VIRTIO] Failed to initialize queue manager\n");
            return NULL;
        }
    }

    if (!dev)
    {
        tiny_log(ERROR, "[VIRTIO] Invalid device pointer\n");
        return NULL;
    }

    if (queue_manager->allocated_count >= VIRTIO_MAX_TOTAL_QUEUES)
    {
        tiny_log(ERROR, "[VIRTIO] No free queues available (max: %d)\n", VIRTIO_MAX_TOTAL_QUEUES);
        return NULL;
    }

    // Find a free queue slot
    for (uint32_t i = 0; i < VIRTIO_MAX_TOTAL_QUEUES; i++)
    {
        if (!queue_manager->queues[i].in_use)
        {
            virtqueue_t *queue = &queue_manager->queues[i];

            // Initialize queue structure
            queue->queue_id = queue_manager->next_queue_id++;
            queue->device_queue_idx = device_queue_idx;
            queue->device = dev;
            queue->in_use = true;
            queue->last_used_idx = 0;
            queue->queue_size = 0; // Will be set during initialization

            queue_manager->allocated_count++;

            return queue;
        }
    }

    tiny_log(ERROR, "[VIRTIO] Failed to find free queue slot\n");
    return NULL;
}

void virtio_queue_free(virtqueue_t *queue)
{
    virtio_queue_manager_t *queue_manager = virtio_get_queue_manager();
    if (!queue || !queue->in_use)
    {
        tiny_log(WARN, "[VIRTIO] Attempt to free invalid or already free queue\n");
        return;
    }

    tiny_log(TRACE, "[VIRTIO] Freeing queue ID %d\n", queue->queue_id);

    // Clear queue structure
    queue->in_use = false;
    queue->queue_id = 0;
    queue->device = NULL;
    queue->desc = NULL;
    queue->avail = NULL;
    queue->used = NULL;

    queue_manager->allocated_count--;
}

virtqueue_t *virtio_queue_get_by_id(uint32_t queue_id)
{
    virtio_queue_manager_t *queue_manager = virtio_get_queue_manager();
    if (!queue_manager_initialized || queue_id == 0)
    {
        return NULL;
    }

    for (uint32_t i = 0; i < VIRTIO_MAX_TOTAL_QUEUES; i++)
    {
        if (queue_manager->queues[i].in_use && queue_manager->queues[i].queue_id == queue_id)
        {
            return &queue_manager->queues[i];
        }
    }

    return NULL;
}

virtqueue_t *virtio_queue_get_device_queue(virtio_device_t *dev, uint32_t device_queue_idx)
{
    virtio_queue_manager_t *queue_manager = virtio_get_queue_manager();
    if (!queue_manager_initialized || !dev)
    {
        return NULL;
    }

    for (uint32_t i = 0; i < VIRTIO_MAX_TOTAL_QUEUES; i++)
    {
        if (queue_manager->queues[i].in_use &&
            queue_manager->queues[i].device == dev &&
            queue_manager->queues[i].device_queue_idx == device_queue_idx)
        {
            return &queue_manager->queues[i];
        }
    }

    return NULL;
}

bool virtio_queue_init(virtqueue_t *queue)
{
    if (!queue || !queue->device)
    {
        tiny_log(ERROR, "[VIRTIO] Invalid queue or device pointer\n");
        return false;
    }

    virtio_device_t *dev = queue->device;
    uint32_t queue_idx = queue->device_queue_idx;

    tiny_log(TRACE, "[VIRTIO] Initializing queue ID %d (device queue %d)\n", queue->queue_id, queue_idx);

    // Select queue
    virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_SEL, queue_idx);

    // Get maximum queue size
    uint32_t queue_num_max = virtio_read32(dev->base_addr + VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_num_max == 0)
    {
        tiny_log(WARN, "[VIRTIO] Queue %d not available\n", queue_idx);
        return false;
    }

    tiny_log(TRACE, "[VIRTIO] Queue %d max size: %d\n", queue_idx, queue_num_max);
    dev->queue_num_max = queue_num_max;

    // Set queue size with safety limit - use very small size for testing
    uint32_t queue_size = queue_num_max;
    if (queue_size > 16)
    {
        queue_size = 16; // Use very small queue size for debugging
        tiny_log(TRACE, "[VIRTIO] Queue size limited to 16 for debugging\n");
    }
    virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_NUM, queue_size);
    tiny_log(TRACE, "[VIRTIO] Queue %d size set to: %d\n", queue_idx, queue_size);

    // VirtIO 1.0 Legacy mode requires all queue components in a single contiguous memory region
    // starting from PFN-specified page. Modern mode uses separate addresses.

    // Calculate sizes for all components
    uint64_t desc_size = queue_size * sizeof(virtq_desc_t); // queue_size * 16
    uint64_t avail_size = 6 + (queue_size * 2);             // 6 + queue_size * 2
    uint64_t used_size = 6 + (queue_size * 2);              // 6 + queue_size * 2

    // Memory layout depends on VirtIO version
    // Allocate unique memory region for each queue to avoid conflicts
    uint64_t base_addr = 0x45000000 + (queue->queue_id * 0x10000); // 64KB per queue
    uint64_t desc_addr, avail_addr, used_addr;

    if (dev->version >= 2)
    {
        // Modern mode: Use separate, optimally aligned memory regions
        desc_addr = base_addr;                            // Descriptor table (16-byte aligned)
        avail_addr = (base_addr + desc_size + 15) & ~15;  // Available ring (2-byte aligned, but use 16 for safety)
        used_addr = (avail_addr + avail_size + 15) & ~15; // Used ring (4-byte aligned, but use 16 for safety)

        tiny_log(TRACE, "[VIRTIO] Modern mode layout - optimized alignment\n");
    }
    else
    {
        // Legacy mode layout (all components in one contiguous region)
        desc_addr = base_addr;                                         // Descriptor table (16-byte aligned)
        avail_addr = desc_addr + desc_size;                            // Available ring follows descriptors
        used_addr = (avail_addr + used_size + 4096 - 1) & ~(4096 - 1); // Used ring 4KB aligned

        tiny_log(TRACE, "[VIRTIO] Legacy mode layout - contiguous memory\n");
    }

    // Verify total size fits in reasonable bounds (should be < 4KB for small queues)
    uint64_t total_size = used_addr + used_size - base_addr;

    tiny_log(TRACE, "[VIRTIO] Legacy layout - Base: 0x%x, Desc: 0x%x, Avail: 0x%x, Used: 0x%x\n",
             (uint32_t)base_addr, (uint32_t)desc_addr, (uint32_t)avail_addr, (uint32_t)used_addr);
    tiny_log(TRACE, "[VIRTIO] Component sizes - Desc: %d, Avail: %d, Used: %d, Total: %d bytes\n",
             (int)desc_size, (int)avail_size, (int)used_size, (int)total_size);
    tiny_log(TRACE, "[VIRTIO] Address offsets - Avail: +%d, Used: +%d\n",
             (int)(avail_addr - base_addr), (int)(used_addr - base_addr));

    if (total_size > 8192)
    {
        tiny_log(WARN, "[VIRTIO] Queue layout exceeds 8KB, this may cause issues\n");
    }
    else
    {
        tiny_log(TRACE, "[VIRTIO] Legacy queue layout validation PASSED (total %d bytes)\n", (int)total_size);
    }

    // Clear entire queue memory region safely
    volatile uint8_t *base_ptr = (volatile uint8_t *)base_addr;

    tiny_log(DEBUG, "[VIRTIO] Clearing entire queue region (%d bytes) from 0x%x...\n",
             (int)total_size, (uint32_t)base_addr);

    // Clear entire memory region byte by byte to avoid crashes
    for (int i = 0; i < total_size; i++)
    {
        base_ptr[i] = 0;
    }

    tiny_log(DEBUG, "[VIRTIO] All queue memory cleared successfully\n");

    // Check VirtIO version for compatibility - prefer Modern mode
    if (dev->version >= 2)
    {
        // VirtIO 1.1+ modern interface
        tiny_log(TRACE, "[VIRTIO] Using VirtIO 1.1+ modern interface (queue_size=%d)\n", queue_size);

        // Ensure all memory regions are properly cleaned before configuration
        virtio_cache_clean_range(desc_addr, desc_size);
        virtio_cache_clean_range(avail_addr, avail_size);
        virtio_cache_clean_range(used_addr, used_size);

        // Set queue addresses with 64-bit addressing support
        tiny_log(DEBUG, "[VIRTIO] Setting descriptor table: 0x%x%08x\n",
                 (uint32_t)(desc_addr >> 32), (uint32_t)desc_addr);
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));

        // Add memory barrier after descriptor table setup
        __asm__ volatile("dmb sy" ::: "memory");

        tiny_log(DEBUG, "[VIRTIO] Setting available ring: 0x%x%08x\n",
                 (uint32_t)(avail_addr >> 32), (uint32_t)avail_addr);
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail_addr);
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32));

        tiny_log(DEBUG, "[VIRTIO] Setting used ring: 0x%x%08x\n",
                 (uint32_t)(used_addr >> 32), (uint32_t)used_addr);
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)used_addr);
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)(used_addr >> 32));

        // Add memory barrier before enabling queue
        __asm__ volatile("dmb sy" ::: "memory");
        __asm__ volatile("dsb sy" ::: "memory");

        // Enable the queue
        tiny_log(DEBUG, "[VIRTIO] Enabling queue...\n");
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_READY, 1);

        // Verify queue is ready with timeout
        uint32_t queue_ready;
        int ready_timeout = 1000;
        do
        {
            queue_ready = virtio_read32(dev->base_addr + VIRTIO_MMIO_QUEUE_READY);
            if (queue_ready == 1)
                break;
            ready_timeout--;
            for (volatile int i = 0; i < 100; i++)
                ; // Small delay
        } while (ready_timeout > 0);

        if (queue_ready != 1)
        {
            tiny_log(WARN, "[VIRTIO] Queue %d failed to become ready (ready=%d, timeout=%d)\n",
                     queue_idx, queue_ready, ready_timeout);
            return false;
        }

        tiny_log(TRACE, "[VIRTIO] Modern mode queue %d successfully activated (ready=%d)\n",
                 queue_idx, queue_ready);
    }
    else
    {
        // VirtIO 1.0 legacy interface - all components in contiguous memory
        tiny_log(TRACE, "[VIRTIO] Using VirtIO 1.0 legacy interface (queue_size=%d)\n", queue_size);

        // Add memory barrier before setting page size
        __asm__ volatile("dmb sy" ::: "memory");

        // Set guest page size (4KB) - required for PFN calculation
        virtio_write32(dev->base_addr + VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);

        // CRITICAL: Set queue alignment - this is required in Legacy mode
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        tiny_log(TRACE, "[VIRTIO] Queue alignment set to 4096 bytes\n");

        // Set queue PFN (physical frame number) - device calculates component addresses automatically
        uint32_t queue_pfn = (uint32_t)(base_addr >> 12); // Use base_addr instead of desc_addr
        virtio_write32(dev->base_addr + VIRTIO_MMIO_QUEUE_PFN, queue_pfn);

        // CRITICAL: Add stronger memory barriers and cache operations after PFN setup
        __asm__ volatile("dmb sy" ::: "memory"); // Data memory barrier
        __asm__ volatile("dsb sy" ::: "memory"); // Data synchronization barrier
        __asm__ volatile("isb" ::: "memory");    // Instruction synchronization barrier

        // Additional cache cleaning to ensure device sees the configuration
        virtio_cache_clean_range(base_addr, total_size);

        tiny_log(TRACE, "[VIRTIO] Legacy mode queue PFN set to: 0x%x (base_addr=0x%x)\n",
                 queue_pfn, (uint32_t)base_addr);
        tiny_log(TRACE, "[VIRTIO] Device will auto-calculate: Desc=0x%x, Avail=0x%x, Used=0x%x\n",
                 (uint32_t)desc_addr, (uint32_t)avail_addr, (uint32_t)used_addr);

        // Verify PFN calculation is correct
        uint32_t pfn_check = virtio_read32(dev->base_addr + VIRTIO_MMIO_QUEUE_PFN);
        if (pfn_check != queue_pfn)
        {
            tiny_log(WARN, "[VIRTIO] PFN readback mismatch: wrote 0x%x, read 0x%x\n", queue_pfn, pfn_check);
        }
        else
        {
            tiny_log(TRACE, "[VIRTIO] PFN readback verification PASSED: 0x%x\n", pfn_check);
        }
    }

    // Initialize queue structure - let device calculate the actual layout according to VirtIO spec
    queue->queue_size = queue_size;
    queue->desc_table_addr = base_addr; // Descriptor table starts at PFN address

    queue->avail_ring_addr = avail_addr;
    queue->used_ring_addr = used_addr;
    queue->last_used_idx = 0;

    // Set up pointers to device-calculated addresses
    queue->desc = (virtq_desc_t *)base_addr;
    queue->avail = (virtq_avail_t *)avail_addr;
    queue->used = (virtq_used_t *)used_addr;

    tiny_log(TRACE, "[VIRTIO] Device-calculated addresses - Desc: 0x%x, Avail: 0x%x, Used: 0x%x\n",
             (uint32_t)base_addr, (uint32_t)avail_addr, (uint32_t)used_addr);

    // CRITICAL: Ensure all queue memory is properly flushed to main memory
    // Clean entire queue region to ensure device sees initialized memory
    virtio_cache_clean_range(base_addr, total_size);

    // Strong memory barrier to ensure all setup is complete before device activation
    __asm__ volatile("dmb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory"); // Instruction synchronization barrier

    // CRITICAL: Set VIRTQ_AVAIL_F_NO_INTERRUPT flag to enable polling mode
    // This tells the device NOT to use interrupts and allows pure polling
#if USE_VIRTIO_IRQ
    queue->avail->flags &= ~VIRTQ_AVAIL_F_NO_INTERRUPT; // Clear any previous flags
#else
    queue->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;
#endif
    tiny_log(TRACE, "[VIRTIO] Set avail->flags = 0x%x for polling mode\n",
             queue->avail->flags);

    // Ensure the flag setting is visible to the device
    virtio_cache_clean_range((uint64_t)queue->avail, sizeof(virtq_avail_t));
    __asm__ volatile("dmb sy" ::: "memory");

    tiny_log(TRACE, "[VIRTIO] Queue ID %d initialization SUCCESSFUL (polling mode enabled)\n", queue->queue_id);
    return true;
}

virtio_device_t *virtio_get_device(void)
{
    return &virtio_dev;
}

// New queue management functions that take queue pointer
bool virtio_queue_add_descriptor(virtqueue_t *queue, uint16_t desc_idx, uint64_t addr, uint32_t len, uint16_t flags, uint16_t next)
{
    if (!queue)
    {
        tiny_log(ERROR, "[VIRTIO] Invalid queue pointer\n");
        return false;
    }

    if (desc_idx >= queue->queue_size)
    {
        tiny_log(WARN, "[VIRTIO] Invalid descriptor index: %d\n", desc_idx);
        return false;
    }

    queue->desc[desc_idx].addr = addr;
    queue->desc[desc_idx].len = len;
    queue->desc[desc_idx].flags = flags;
    queue->desc[desc_idx].next = next;

    tiny_log(DEBUG, "[VIRTIO] Queue %d: Added descriptor %d: addr=0x%x, len=%d, flags=0x%x\n",
             queue->queue_id, desc_idx, (uint32_t)addr, len, flags);

    return true;
}

bool virtio_queue_submit_request(virtqueue_t *queue, uint16_t desc_head)
{
    // tiny_log(ERROR, "queue->device->base_addr : %x addr: %x\n", queue->device->base_addr, queue);

    if (!queue || !queue->device)
    {
        tiny_log(ERROR, "[VIRTIO] Invalid queue or device pointer\n");
        return false;
    }

    // Cache management: Clean descriptor table and data buffers before submission
    virtio_cache_clean_range((uint64_t)queue->desc,
                             queue->queue_size * sizeof(virtq_desc_t));

    // Clean the specific descriptors in the chain
    uint16_t current_desc = desc_head;
    while (current_desc < queue->queue_size)
    {
        virtq_desc_t *desc = &queue->desc[current_desc];

        // Clean the data buffer pointed to by this descriptor
        virtio_cache_clean_range(desc->addr, desc->len);

        tiny_log(DEBUG, "[VIRTIO] Queue %d: Cleaned descriptor %d buffer: addr=0x%x, len=%d\n",
                 queue->queue_id, current_desc, (uint32_t)desc->addr, desc->len);

        if (!(desc->flags & VIRTQ_DESC_F_NEXT))
        {
            break;
        }
        current_desc = desc->next;
    }

    // Add to available ring
    uint16_t avail_idx = queue->avail->idx;
    queue->avail->ring[avail_idx % queue->queue_size] = desc_head;

#if USE_VIRTIO_IRQ
    queue->avail->flags &= ~VIRTQ_AVAIL_F_NO_INTERRUPT; // Clear any previous flags
#else
    queue->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;
#endif
    tiny_log(TRACE, "[VIRTIO] Queue %d: Set avail->flags = 0x%x for polling mode\n",
             queue->queue_id, queue->avail->flags);

    // Memory barrier - critical for ARM architecture
    __asm__ volatile("dmb sy" ::: "memory");

    // Update available index
    queue->avail->idx = avail_idx + 1;

    tiny_log(DEBUG, "[VIRTIO] Queue %d: Request queued: desc_head=%d, avail_idx=%d->%d, flags=0x%x\n",
             queue->queue_id, desc_head, avail_idx, avail_idx + 1, queue->avail->flags);

    // Clean available ring to ensure device can see the update
    virtio_cache_clean_range((uint64_t)queue->avail,
                             sizeof(virtq_avail_t) + queue->queue_size * sizeof(uint16_t));

    // Another memory barrier to ensure index update is visible
    __asm__ volatile("dmb sy" ::: "memory");

    tiny_log(DEBUG, "[VIRTIO] Queue %d: Submitted request: desc_head=%d, avail_idx=%d\n",
             queue->queue_id, desc_head, avail_idx);

#if USE_VIRTIO_IRQ
    virtio_reset_interrupt_state();
#endif
    // tiny_log(ERROR, "queue->device->base_addr : %x", queue->device->base_addr);

    // Notify device - use the device queue index
    virtio_write32(queue->device->base_addr + VIRTIO_MMIO_QUEUE_NOTIFY, queue->device_queue_idx);

    // CRITICAL: Add strong memory barriers after device notification
    __asm__ volatile("dmb sy" ::: "memory"); // Data memory barrier
    __asm__ volatile("dsb sy" ::: "memory"); // Data synchronization barrier
    __asm__ volatile("isb" ::: "memory");    // Instruction synchronization barrier

    tiny_log(DEBUG, "[VIRTIO] Queue %d: Device notified with queue index %d\n",
             queue->queue_id, queue->device_queue_idx);

    return true;
}

bool virtio_queue_wait_for_completion(virtqueue_t *queue)
{
    if (!queue)
    {
        tiny_log(ERROR, "[VIRTIO] Invalid queue pointer\n");
        return false;
    }

    tiny_log(DEBUG, "[VIRTIO] Queue %d: Starting wait loop...\n", queue->queue_id);

    uint32_t timeout = 1000000; // Timeout counter
    uint32_t debug_counter = 0;

    while (timeout > 0)
    {
        // Invalidate used ring cache before checking for updates
        virtio_cache_invalidate_range((uint64_t)queue->used,
                                      sizeof(virtq_used_t) + queue->queue_size * sizeof(virtq_used_elem_t));

        // Safe access to used index
        volatile uint16_t used_idx = queue->used->idx;

        // Periodic debug output
        if (debug_counter % 100000 == 0)
        {
            tiny_log(DEBUG, "[VIRTIO] Queue %d: Checking... used_idx=%d, last_used_idx=%d, timeout=%d\n",
                     queue->queue_id, used_idx, queue->last_used_idx, timeout);
        }

        if (used_idx != queue->last_used_idx)
        {
            tiny_log(DEBUG, "[VIRTIO] Queue %d: Request completed: used_idx=%d, last_used_idx=%d\n",
                     queue->queue_id, used_idx, queue->last_used_idx);

            // Process completed requests safely
            while (queue->last_used_idx != used_idx)
            {
                uint16_t used_ring_idx = queue->last_used_idx % queue->queue_size;

                // Safe access to used ring element
                volatile virtq_used_elem_t *used_elem = &queue->used->ring[used_ring_idx];
                uint32_t elem_id = used_elem->id;
                uint32_t elem_len = used_elem->len;

                tiny_log(DEBUG, "[VIRTIO] Queue %d: Completed descriptor %d, length %d\n",
                         queue->queue_id, elem_id, elem_len);

                queue->last_used_idx++;
            }

            return true;
        }

        timeout--;
        debug_counter++;
        // Small delay
        for (volatile int i = 0; i < 1000; i++)
            ;
    }

    tiny_log(WARN, "[VIRTIO] Queue %d: Request timeout\n", queue->queue_id);
    return false;
}

uint64_t virtio_scan_devices(uint32_t target_device_id)
{
    tiny_log(TRACE, "[VIRTIO] Scanning for device ID %d across %d slots\n",
             target_device_id, VIRTIO_MMIO_MAX_DEVICES);

    for (uint32_t slot = 0; slot < VIRTIO_MMIO_MAX_DEVICES; slot++)
    {
        uint64_t base_addr = VIRTIO_MMIO_BASE_ADDR + (slot * VIRTIO_MMIO_DEVICE_SIZE);

        tiny_log(DEBUG, "[VIRTIO] Checking slot %d at address 0x%x\n", slot, (uint32_t)base_addr);

        // Read magic number
        uint32_t magic = virtio_read32(base_addr + VIRTIO_MMIO_MAGIC);
        if (magic != VIRTIO_MAGIC_VALUE)
        {
            tiny_log(DEBUG, "[VIRTIO] Slot %d: Invalid magic 0x%x (expected 0x%x)\n",
                     slot, magic, VIRTIO_MAGIC_VALUE);
            continue;
        }

        // Read version
        uint32_t version = virtio_read32(base_addr + VIRTIO_MMIO_VERSION);
        if (version < 1 || version > 2)
        {
            tiny_log(DEBUG, "[VIRTIO] Slot %d: Unsupported version %d (supported: 1-2)\n", slot, version);
            continue;
        }

        // Read device ID
        uint32_t device_id = virtio_read32(base_addr + VIRTIO_MMIO_DEVICE_ID);
        tiny_log(DEBUG, "[VIRTIO] Slot %d: Found device ID %d (magic=0x%x, version=%d)\n",
                 slot, device_id, magic, version);

        if (device_id == 0)
        {
            tiny_log(DEBUG, "[VIRTIO] Slot %d: Empty slot (device_id = 0)\n", slot);
            continue;
        }

        if (device_id == target_device_id)
        {
            tiny_log(TRACE, "[VIRTIO] Found target device ID %d at slot %d (address 0x%x)\n",
                     target_device_id, slot, (uint32_t)base_addr);
            return base_addr;
        }
        else
        {
            tiny_log(DEBUG, "[VIRTIO] Slot %d: Device ID %d does not match target %d\n",
                     slot, device_id, target_device_id);
        }
    }

    tiny_log(WARN, "[VIRTIO] Device ID %d not found in any of %d slots\n",
             target_device_id, VIRTIO_MMIO_MAX_DEVICES);
    return 0;
}
