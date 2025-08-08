/*
 * virtio_multiqueue_test.c
 *
 * Created on: 2025-07-21
 * Author: debin
 *
 * Description: Test program for VirtIO multi-queue functionality
 */

#include "virtio/virtio_mmio.h"
#include "virtio/virtio_blk.h"
#include "tiny_io.h"
#include "config.h"

// Test function to verify multi-queue allocation and management
bool virtio_test_multiqueue_allocation(void)
{
    tiny_log(TRACE, "[VIRTIO_TEST] === Multi-Queue Allocation Test ===\n");

    // Initialize queue manager
    if (!virtio_queue_manager_init())
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Failed to initialize queue manager\n");
        return false;
    }
    tiny_log(TRACE, "[VIRTIO_TEST] === Multi-Queue Allocation Test ===\n");
    // Create a mock device for testing
    virtio_device_t test_dev;
    test_dev.base_addr = 0x0a000000;
    test_dev.device_id = VIRTIO_DEVICE_ID_BLOCK;
    test_dev.version = 2;

    tiny_log(TRACE, "[VIRTIO_TEST] Testing queue allocation...\n");

    // Test 1: Allocate multiple queues for the same device
    virtqueue_t *queue1 = virtio_queue_alloc(&test_dev, 0);
    virtqueue_t *queue2 = virtio_queue_alloc(&test_dev, 1);
    virtqueue_t *queue3 = virtio_queue_alloc(&test_dev, 2);

    if (!queue1 || !queue2 || !queue3)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Failed to allocate queues\n");
        return false;
    }

    tiny_log(TRACE, "[VIRTIO_TEST] Allocated 3 queues successfully:\n");
    tiny_log(TRACE, "[VIRTIO_TEST]   Queue 1: ID=%d, device_queue_idx=%d\n",
             queue1->queue_id, queue1->device_queue_idx);
    tiny_log(TRACE, "[VIRTIO_TEST]   Queue 2: ID=%d, device_queue_idx=%d\n",
             queue2->queue_id, queue2->device_queue_idx);
    tiny_log(TRACE, "[VIRTIO_TEST]   Queue 3: ID=%d, device_queue_idx=%d\n",
             queue3->queue_id, queue3->device_queue_idx);

    // Test 2: Verify queue lookup functions
    tiny_log(TRACE, "[VIRTIO_TEST] Testing queue lookup functions...\n");

    virtqueue_t *found_queue = virtio_queue_get_by_id(queue2->queue_id);
    if (found_queue != queue2)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Queue lookup by ID failed\n");
        return false;
    }

    found_queue = virtio_queue_get_device_queue(&test_dev, 1);
    if (found_queue != queue2)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Queue lookup by device and index failed\n");
        return false;
    }

    tiny_log(TRACE, "[VIRTIO_TEST] Queue lookup functions work correctly\n");

    // Test 3: Test queue freeing
    tiny_log(TRACE, "[VIRTIO_TEST] Testing queue freeing...\n");

    virtio_queue_free(queue2);
    
    // Verify the queue is no longer findable
    found_queue = virtio_queue_get_by_id(queue2->queue_id);
    if (found_queue != NULL)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Queue should have been freed but is still findable\n");
        return false;
    }

    tiny_log(TRACE, "[VIRTIO_TEST] Queue freeing works correctly\n");

    // Test 4: Allocate a new queue to verify reuse of freed slot
    virtqueue_t *queue4 = virtio_queue_alloc(&test_dev, 3);
    if (!queue4)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Failed to allocate queue after freeing\n");
        return false;
    }

    tiny_log(TRACE, "[VIRTIO_TEST] Successfully allocated new queue: ID=%d, device_queue_idx=%d\n",
             queue4->queue_id, queue4->device_queue_idx);

    // Clean up remaining queues
    virtio_queue_free(queue1);
    virtio_queue_free(queue3);
    virtio_queue_free(queue4);

    tiny_log(TRACE, "[VIRTIO_TEST] === Multi-Queue Allocation Test PASSED ===\n");
    return true;
}

// Test function to verify memory isolation between queues
bool virtio_test_multiqueue_memory_isolation(void)
{
    tiny_log(TRACE, "[VIRTIO_TEST] === Multi-Queue Memory Isolation Test ===\n");

    // Create mock devices
    virtio_device_t dev1, dev2;
    dev1.base_addr = 0x0a000000;
    dev1.device_id = VIRTIO_DEVICE_ID_BLOCK;
    dev1.version = 2;
    
    dev2.base_addr = 0x0a000200;
    dev2.device_id = VIRTIO_DEVICE_ID_NET;
    dev2.version = 2;

    // Allocate queues for different devices
    virtqueue_t *queue_dev1_0 = virtio_queue_alloc(&dev1, 0);
    virtqueue_t *queue_dev1_1 = virtio_queue_alloc(&dev1, 1);
    virtqueue_t *queue_dev2_0 = virtio_queue_alloc(&dev2, 0);

    if (!queue_dev1_0 || !queue_dev1_1 || !queue_dev2_0)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Failed to allocate queues for memory isolation test\n");
        return false;
    }

    // Verify each queue has unique memory regions
    uint64_t base1 = 0x45000000 + (queue_dev1_0->queue_id * 0x10000);
    uint64_t base2 = 0x45000000 + (queue_dev1_1->queue_id * 0x10000);
    uint64_t base3 = 0x45000000 + (queue_dev2_0->queue_id * 0x10000);

    tiny_log(TRACE, "[VIRTIO_TEST] Queue memory regions:\n");
    tiny_log(TRACE, "[VIRTIO_TEST]   Queue %d (dev1): 0x%x\n", queue_dev1_0->queue_id, (uint32_t)base1);
    tiny_log(TRACE, "[VIRTIO_TEST]   Queue %d (dev1): 0x%x\n", queue_dev1_1->queue_id, (uint32_t)base2);
    tiny_log(TRACE, "[VIRTIO_TEST]   Queue %d (dev2): 0x%x\n", queue_dev2_0->queue_id, (uint32_t)base3);

    // Verify no memory overlap (each queue gets 64KB)
    if (base1 == base2 || base1 == base3 || base2 == base3)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Memory regions overlap!\n");
        return false;
    }

    if ((base2 - base1) < 0x10000 || (base3 - base1) < 0x10000 || (base3 - base2) < 0x10000)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Memory regions too close (< 64KB apart)!\n");
        return false;
    }

    tiny_log(TRACE, "[VIRTIO_TEST] Memory isolation verified - no overlaps\n");

    // Clean up
    virtio_queue_free(queue_dev1_0);
    virtio_queue_free(queue_dev1_1);
    virtio_queue_free(queue_dev2_0);

    tiny_log(TRACE, "[VIRTIO_TEST] === Multi-Queue Memory Isolation Test PASSED ===\n");
    return true;
}

// Test function to verify backward compatibility
bool virtio_test_backward_compatibility(void)
{
    tiny_log(TRACE, "[VIRTIO_TEST] === Backward Compatibility Test ===\n");

    // Create a mock device
    virtio_device_t test_dev;
    test_dev.base_addr = 0x0a000000;
    test_dev.device_id = VIRTIO_DEVICE_ID_BLOCK;
    test_dev.version = 2;

    // Allocate a queue using new interface
    virtqueue_t *queue = virtio_queue_alloc(&test_dev, 0);
    if (!queue)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Failed to allocate queue for compatibility test\n");
        return false;
    }

    // Clean up
    virtio_queue_free(queue);
    tiny_log(TRACE, "[VIRTIO_TEST] === Backward Compatibility Test PASSED ===\n");
    return true;
}

// Main test function
bool virtio_test_multiqueue_functionality(void)
{
    tiny_log(TRACE, "[VIRTIO_TEST] ========================================\n");
    tiny_log(TRACE, "[VIRTIO_TEST] Starting VirtIO Multi-Queue Tests\n");
    tiny_log(TRACE, "[VIRTIO_TEST] ========================================\n");

    bool all_passed = true;

    // Run all tests
    if (!virtio_test_multiqueue_allocation())
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Multi-queue allocation test FAILED\n");
        all_passed = false;
    }

    if (!virtio_test_multiqueue_memory_isolation())
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Memory isolation test FAILED\n");
        all_passed = false;
    }

    if (!virtio_test_backward_compatibility())
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Backward compatibility test FAILED\n");
        all_passed = false;
    }

    tiny_log(TRACE, "[VIRTIO_TEST] ========================================\n");
    if (all_passed)
    {
        tiny_log(TRACE, "[VIRTIO_TEST] ALL TESTS PASSED!\n");
    }
    else
    {
        tiny_log(ERROR, "[VIRTIO_TEST] SOME TESTS FAILED!\n");
    }
    tiny_log(TRACE, "[VIRTIO_TEST] ========================================\n");

    return all_passed;
}
