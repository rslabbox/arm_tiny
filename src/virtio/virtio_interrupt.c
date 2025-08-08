/*
 * virtio_interrupt.c
 *
 * VirtIO interrupt-driven I/O implementation
 *
 * This module provides interrupt-based VirtIO device communication,
 * replacing the timeout-prone polling mechanism with efficient
 * interrupt-driven notifications.
 *
 * Author: Debin
 * Date: 2025-06-20
 */

#include "virtio/virtio_interrupt.h"
#include "virtio/virtio_mmio.h"
#include "gicv2.h"
#include "handle.h"
#include "config.h"
#include "tiny_io.h"

// External reference to interrupt handler vector
extern irq_handler_t g_handler_vec[512];

// Global VirtIO interrupt state
virtio_interrupt_state_t virtio_irq_state = {
    .interrupt_received = false,
    .interrupt_status = 0,
    .interrupt_count = 0,
    .last_used_idx = 0,
    .interrupts_enabled = false,
    .active_irq_number = 0}; // No active IRQ initially

bool virtio_interrupt_init(void)
{
    tiny_log(TRACE, "[VIRTIO_IRQ] Initializing VirtIO interrupt system\n");

    // Reset interrupt state
    virtio_reset_interrupt_state();

    // Handle VirtIO interrupt
    irq_handle_register(VIRTIO_IRQ_0, virtio_irq_handler);

    // Enable global interrupts
    // Enable the VirtIO interrupt
    gic_enable_int(VIRTIO_IRQ_0, 0);

    // Verify GIC configuration
    bool gic_enabled = gic_get_enable(VIRTIO_IRQ_0);
    tiny_log(TRACE, "[VIRTIO_TEST] GIC enable status for IRQ %d: %s\n",
             VIRTIO_IRQ_0, gic_enabled ? "ENABLED" : "DISABLED");

    if (!gic_enabled)
    {
        tiny_log(ERROR, "[VIRTIO_TEST] Failed to enable IRQ %d in GIC\n", VIRTIO_IRQ_0);
        return false;
    }
    enable_interrupts();
    tiny_log(TRACE, "[VIRTIO_IRQ] Global interrupts enabled\n");

    // Mark interrupts as enabled
    virtio_irq_state.interrupts_enabled = true;

    tiny_log(TRACE, "[VIRTIO_IRQ] VirtIO interrupt system initialization COMPLETED\n");

    return true;
}

void virtio_ready_interrupts(void)
{
    virtio_irq_state.interrupt_received = false;
    tiny_log(TRACE, "[VIRTIO_IRQ] VirtIO interrupts are ready\n");
}

void virtio_irq_handler(uint64_t *ctx)
{
    // Increment interrupt counter first
    virtio_irq_state.interrupt_count++;

    tiny_log(TRACE, "[VIRTIO_IRQ] *** VirtIO INTERRUPT #%d RECEIVED ***\n",
             virtio_irq_state.interrupt_count);

    // Get VirtIO device for interrupt processing
    virtio_device_t *dev = virtio_get_blk_device();
    if (!dev)
    {
        tiny_log(ERROR, "[VIRTIO_IRQ] ERROR: No VirtIO device available in interrupt handler\n");
        return;
    }

    tiny_log(TRACE, "[VIRTIO_IRQ] Processing interrupt for device at 0x%x\n", dev->base_addr);

    // Read interrupt status from VirtIO device
    uint32_t interrupt_status = virtio_read32(dev->base_addr + VIRTIO_MMIO_INTERRUPT_STATUS);
    virtio_irq_state.interrupt_status = interrupt_status;

    tiny_log(TRACE, "[VIRTIO_IRQ] Interrupt status register: 0x%x\n", interrupt_status);

    // Decode interrupt status bits with detailed logging
    if (interrupt_status & VIRTIO_IRQ_VRING_UPDATE)
    {
        tiny_log(TRACE, "[VIRTIO_IRQ] VRING_UPDATE interrupt: Used buffer notification\n");
    }
    if (interrupt_status & VIRTIO_IRQ_CONFIG_CHANGE)
    {
        tiny_log(TRACE, "[VIRTIO_IRQ] CONFIG_CHANGE interrupt: Device configuration changed\n");
    }
    if (interrupt_status == 0)
    {
        tiny_log(WARN, "[VIRTIO_IRQ] WARNING: Spurious interrupt (status=0)\n");
    }

    // Acknowledge interrupt to VirtIO device
    if (interrupt_status != 0)
    {
        tiny_log(DEBUG, "[VIRTIO_IRQ] Acknowledging interrupt status 0x%x\n", interrupt_status);
        virtio_write32(dev->base_addr + VIRTIO_MMIO_INTERRUPT_ACK, interrupt_status);

        // Verify acknowledgment
        uint32_t status_after_ack = virtio_read32(dev->base_addr + VIRTIO_MMIO_INTERRUPT_STATUS);
        tiny_log(DEBUG, "[VIRTIO_IRQ] Status after ACK: 0x%x (should be 0)\n", status_after_ack);
    }

    // Set interrupt received flag (this wakes up waiting functions)
    virtio_irq_state.interrupt_received = true;

    tiny_log(TRACE, "[VIRTIO_IRQ] Interrupt processing COMPLETED successfully\n");
}

bool virtio_wait_for_interrupt(uint32_t timeout_ms)
{
    tiny_log(TRACE, "[VIRTIO_WAIT] Starting interrupt-based wait (timeout: %d ms)\n", timeout_ms);

    // Calculate timeout in loop iterations (approximate)
    uint32_t timeout_loops = timeout_ms * 1000; // Approximate 1000 loops per ms
    uint32_t loop_count = 0;
    uint32_t debug_interval = timeout_loops / 10; // Debug output every 10%

    tiny_log(DEBUG, "[VIRTIO_WAIT] Timeout loops: %d, debug interval: %d\n",
             timeout_loops, debug_interval);

    while (!virtio_irq_state.interrupt_received && loop_count < timeout_loops)
    {

        // Periodic debug output
        if (debug_interval > 0 && (loop_count % debug_interval) == 0)
        {
            uint32_t progress = (loop_count * 100) / timeout_loops;
            tiny_log(DEBUG, "[VIRTIO_WAIT] Progress: %d%% (%d/%d loops)\n",
                     progress, loop_count, timeout_loops);
        }

        // Small delay to prevent busy-waiting
        for (volatile int i = 0; i < 100; i++)
        {
            // Memory barrier to ensure interrupt flag is re-read
            __asm__ volatile("" ::: "memory");
        }

        loop_count++;
    }

    if (virtio_irq_state.interrupt_received)
    {
        // Calculate milliseconds using integer arithmetic (avoid float)
        uint32_t ms_elapsed = loop_count / 1000;     // Integer division for ms
        uint32_t us_remainder = (loop_count % 1000); // Microsecond remainder
        tiny_log(TRACE, "[VIRTIO_WAIT] SUCCESS: Interrupt received after %d loops (%d.%d ms)\n",
                 loop_count, ms_elapsed, us_remainder);
        tiny_log(TRACE, "[VIRTIO_WAIT] Interrupt status: 0x%x, count: %d\n",
                 virtio_irq_state.interrupt_status, virtio_irq_state.interrupt_count);
        return true;
    }
    else
    {
        tiny_log(WARN, "[VIRTIO_WAIT] TIMEOUT: No interrupt received within %d ms (%d loops)\n",
                 timeout_ms, timeout_loops);
        virtio_print_interrupt_stats();
        return false;
    }
}

uint32_t virtio_get_interrupt_status(void)
{
    virtio_device_t *dev = virtio_get_blk_device();
    if (!dev)
    {
        tiny_log(ERROR, "[VIRTIO_IRQ] ERROR: No VirtIO device for status read\n");
        return 0;
    }

    uint32_t status = virtio_read32(dev->base_addr + VIRTIO_MMIO_INTERRUPT_STATUS);
    tiny_log(DEBUG, "[VIRTIO_IRQ] Current interrupt status: 0x%x\n", status);
    return status;
}

void virtio_reset_interrupt_state(void)
{
    tiny_log(DEBUG, "[VIRTIO_IRQ] Resetting interrupt state\n");

    virtio_irq_state.interrupt_received = false;
    virtio_irq_state.interrupt_status = 0;
    virtio_irq_state.interrupt_count = 0;
    virtio_irq_state.last_used_idx = 0;
    virtio_irq_state.interrupts_enabled = false;

    tiny_log(DEBUG, "[VIRTIO_IRQ] Interrupt state reset completed\n");
}

void virtio_print_interrupt_stats(void)
{
    tiny_log(TRACE, "[VIRTIO_IRQ] === VirtIO Interrupt Statistics ===\n");
    tiny_log(TRACE, "[VIRTIO_IRQ] Target IRQ number: %d (calculated for slot 31)\n", VIRTIO_IRQ_0);
    tiny_log(TRACE, "[VIRTIO_IRQ] Interrupts enabled: %s\n",
             virtio_irq_state.interrupts_enabled ? "YES" : "NO");
    tiny_log(TRACE, "[VIRTIO_IRQ] Total interrupts received: %d\n",
             virtio_irq_state.interrupt_count);
    tiny_log(TRACE, "[VIRTIO_IRQ] Last interrupt status: 0x%x\n",
             virtio_irq_state.interrupt_status);
    tiny_log(TRACE, "[VIRTIO_IRQ] Interrupt received flag: %s\n",
             virtio_irq_state.interrupt_received ? "TRUE" : "FALSE");
    tiny_log(TRACE, "[VIRTIO_IRQ] Last used index: %d\n",
             virtio_irq_state.last_used_idx);

    // Check current GIC state for our specific IRQ
    uint32_t target_irq = VIRTIO_IRQ_0;
    int gic_enabled = gic_get_enable(target_irq);
    tiny_log(TRACE, "[VIRTIO_IRQ] GIC interrupt %d enabled: %s\n",
             target_irq, gic_enabled ? "YES" : "NO");

    // Check current device interrupt status
    uint32_t current_status = virtio_get_interrupt_status();
    tiny_log(TRACE, "[VIRTIO_IRQ] Current interrupt status: 0x%x\n", current_status);

    // Additional system diagnostics
    tiny_log(TRACE, "[VIRTIO_IRQ] === System State Diagnostics ===\n");

    // Check VirtIO device configuration
    virtio_device_t *dev = virtio_get_blk_device();
    if (dev)
    {
        uint32_t device_status = virtio_read32(dev->base_addr + VIRTIO_MMIO_STATUS);
        tiny_log(TRACE, "[VIRTIO_IRQ] VirtIO device status: 0x%x\n", device_status);

        // Check if device is in DRIVER_OK state
        if (device_status & VIRTIO_STATUS_DRIVER_OK)
        {
            tiny_log(TRACE, "[VIRTIO_IRQ] Device is DRIVER_OK\n");
        }
        else
        {
            tiny_log(WARN, "[VIRTIO_IRQ] Device is NOT DRIVER_OK (current: 0x%x)\n", device_status);
        }

        // Check queue configuration
        virtqueue_t *queue = virtio_queue_get_device_queue(dev, 0);
        if (queue && queue->avail)
        {
            tiny_log(TRACE, "[VIRTIO_IRQ] Queue avail flags: 0x%x (0=interrupts enabled)\n",
                     queue->avail->flags);
        }
    }

    // Check handler registration
    if (g_handler_vec[VIRTIO_IRQ_0] != 0)
    {
        tiny_log(TRACE, "[VIRTIO_IRQ] Handler registered for IRQ %d: 0x%x\n",
                 VIRTIO_IRQ_0, (uint64_t)g_handler_vec[VIRTIO_IRQ_0]);
    }
    else
    {
        tiny_log(ERROR, "[VIRTIO_IRQ] NO HANDLER registered for IRQ %d!\n", VIRTIO_IRQ_0);
    }

    tiny_log(TRACE, "[VIRTIO_IRQ] =====================================\n");
}
