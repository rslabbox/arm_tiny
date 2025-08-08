/*
 * File: main.c
 * Author: Debin
 * Date: 2024-11-15
 * Description: Main file for the ARM Tiny project.
 */

#include "tiny_io.h"
#include "gicv2.h"
#include "timer.h"
#include "virtio/virtio_blk.h"
#include "virtio/fat32.h"
#include "virtio/virtio_debug.h"
#include "virtio/virtio_interrupt.h"
#include "virtio/virtio_multiqueue_test.h"
#include "config.h"

#ifndef VM_VERSION
#define VM_VERSION "null"
#endif

// Simple strlen implementation for bare metal environment
static uint32_t my_strlen(const char *str)
{
    uint32_t len = 0;
    while (str[len] != '\0')
    {
        len++;
    }
    return len;
}

int kernel_main(void)
{
    tiny_io_init();
    tiny_log(INFO, "\nHello, ARM Tiny VM %s!\n", VM_VERSION);

#if USE_VIRTIO_IRQ
    // Initialize GIC
    tiny_log(INFO, "=== Initializing GIC ===\n");
    gic_init();

    // Initialize Timer
    tiny_log(INFO, "=== Initializing Timer ===\n");
    timer_init();

    // Test Timer interrupts
    tiny_log(INFO, "=== Testing Timer Interrupts ===\n");
    if (!timer_test_simple())
    {
        tiny_log(WARN, "Timer interrupt test FAILED\n");
        goto error_exit;
    }
    tiny_log(INFO, "Timer interrupt test PASSED\n");
    // Initialize VirtIO Interrupts
    tiny_log(INFO, "=== Initializing VirtIO Interrupts ===\n");
    if (!virtio_interrupt_init())
    {
        tiny_log(WARN, "VirtIO interrupt initialization FAILED\n");
        goto error_exit;
    }
    tiny_log(INFO, "VirtIO interrupt initialization SUCCESSFUL\n");
#else
    tiny_log(INFO, "VirtIO interrupts are disabled (USE_VIRTIO_IRQ=0)\n");
#endif

    tiny_log(INFO, "Starting VirtIO Debug Tests...\n");
    // Test 1: Basic hang point isolation
    tiny_log(INFO, "=== Testing Hang Points ===\n");
    if (!virtio_test_hang_points())
    {
        tiny_log(WARN, "Hang point tests FAILED\n");
        goto error_exit;
    }

    // Test 1.5: Multi-Queue Functionality Test
    tiny_log(INFO, "=== Testing VirtIO Multi-Queue Functionality ===\n");
    if (!virtio_test_multiqueue_functionality())
    {
        tiny_log(WARN, "Multi-queue tests FAILED\n");
        goto error_exit;
    }
    tiny_log(INFO, "Multi-queue tests PASSED\n");

    // Test 2: Initialize VirtIO and test basic access
    tiny_log(INFO, "=== Testing VirtIO Initialization ===\n");
    if (!virtio_blk_init())
    {
        tiny_log(WARN, "VirtIO Block device initialization FAILED\n");
        goto error_exit;
    }

    // Test 3: Basic device access
    tiny_log(INFO, "=== Testing Basic Device Access ===\n");
    if (!virtio_test_basic_access())
    {
        tiny_log(WARN, "Basic access test FAILED\n");
        goto error_exit;
    }

    // Test 5: VirtIO Block Sector Read Test
    tiny_log(INFO, "=== Testing VirtIO Block Sector Read ===\n");
    uint8_t sector_buffer[512];
    if (!virtio_blk_read_sector(0, sector_buffer))
    {
        tiny_log(ERROR, "Failed to read sector 0 (boot sector)\n");
        goto error_exit;
    }

    tiny_log(INFO, "Successfully read boot sector (sector 0)\n");
    tiny_log(DEBUG, "Boot sector first 64 bytes:\n");
    for (int i = 0; i < 64; i++)
    {
        if (i % 16 == 0)
            printf_ext("%4x: ", i);
        printf_ext("%2x ", sector_buffer[i]);
        if (i % 16 == 15)
            printf_ext("\n");
    }

    // Test 6: FAT32 File System Test
    tiny_log(INFO, "=== Testing FAT32 File System ===\n");
    if (!fat32_init())
    {
        tiny_log(ERROR, "Failed to initialize FAT32 file system\n");
        goto error_exit;
    }
    tiny_log(INFO, "FAT32 file system initialized successfully\n");

    // Test 7: File Writing Test
    tiny_log(INFO, "=== Testing File Writing ===\n");
    const char *test_data = "Hello, this is a test file created by the FAT32 implementation!\nThis file contains multiple lines.\nLine 10086 of the test file.";
    if (!fat32_write_file("hello.txt", test_data, my_strlen(test_data)))
    {
        tiny_log(WARN, "Failed to write test.txt file\n");
    }
    else
    {
        tiny_log(INFO, "Successfully wrote test.txt file\n");
    }

    // Test 8: File Reading Test
    tiny_log(INFO, "=== Testing File Reading ===\n");
    char file_content[512];
    if (!fat32_read_file("hello.txt", file_content, 511))
    {
        tiny_log(WARN, "Failed to read hello.txt file\n");
    }
    else
    {
        file_content[511] = '\0'; // Ensure null termination
        tiny_log(INFO, "Successfully read test.txt file\n");
        tiny_log(INFO, "File content: %s\n", file_content);
    }

    // Test 9: Disk Performance Test
    tiny_log(INFO, "=== Testing Disk Performance ===\n");
    if (!virtio_blk_performance_test())
    {
        tiny_log(WARN, "Disk performance test FAILED\n");
        goto error_exit;
    }
    tiny_log(INFO, "Disk performance test PASSED\n");

    tiny_log(INFO, "All VirtIO tests completed successfully!\n");
    printf_ext("All");

    // Shutdown the system automatically
    system_shutdown();

    return 0;

error_exit:
    tiny_log(WARN, "System stopped due to error\n");

    // Shutdown the system even on error
    system_shutdown();

    return -1;
}
