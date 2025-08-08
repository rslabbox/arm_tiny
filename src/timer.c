/*
 * timer.c
 *
 * Simple ARM Generic Timer implementation for interrupt testing
 *
 * Author: Debin
 * Date: 2025-06-19
 */

#include "timer.h"
#include "gicv2.h"
#include "handle.h"
#include "tiny_io.h"

// Global timer interrupt counter
volatile uint32_t timer_interrupt_count = 0;

// Helper functions to read/write system registers
static inline uint64_t read_cntfrq_el0(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, " CNTFRQ_EL0 : "=r"(val));
    return val;
}

static inline uint64_t read_cntpct_el0(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, " CNTPCT_EL0 : "=r"(val));
    return val;
}

static inline void write_cntp_ctl_el0(uint64_t val)
{
    __asm__ volatile("msr " CNTP_CTL_EL0 ", %0" ::"r"(val));
}

static inline void write_cntp_tval_el0(uint64_t val)
{
    __asm__ volatile("msr " CNTP_TVAL_EL0 ", %0" ::"r"(val));
}

static inline uint32_t read_cntp_ctl_el0(void)
{
    uint32_t val;
    __asm__ volatile("mrs %0, " CNTP_CTL_EL0 : "=r"(val));
    return val;
}

void timer_irq_handler(uint64_t *ctx)
{
    timer_interrupt_count++;

    tiny_log(WARN, "[TIMER] Timer interrupt #%d triggered\n", timer_interrupt_count);

    // Disable timer to prevent continuous interrupts
    write_cntp_ctl_el0(CNTP_CTL_IMASK);

    tiny_log(WARN, "[TIMER] Timer interrupt handled and disabled\n");
}

void timer_init(void)
{
    tiny_log(DEBUG, "[TIMER] Initializing ARM Generic Timer\n");

    // Reset interrupt counter
    timer_interrupt_count = 0;

    // Register timer interrupt handler
    irq_handle_register(CNTP_TIMER, timer_irq_handler);

    // Enable timer interrupt in GIC
    gic_enable_int(CNTP_TIMER, 0);

    // Get timer frequency
    uint64_t freq = read_cntfrq_el0();
    tiny_log(DEBUG, "[TIMER] Timer frequency: %llu Hz\n", freq);

    // Initially disable timer
    write_cntp_ctl_el0(CNTP_CTL_IMASK);

    tiny_log(DEBUG, "[TIMER] Timer initialization completed\n");
}

bool timer_test_simple(void)
{
    tiny_log(DEBUG, "[TIMER] Starting simple timer interrupt test\n");

    uint32_t initial_count = timer_interrupt_count;
    tiny_log(DEBUG, "[TIMER] Initial interrupt count: %d\n", initial_count);

    // Get timer frequency for calculating timeout
    uint64_t freq = read_cntfrq_el0();
    uint64_t timeout_ticks = freq / 10; // 100ms timeout

    tiny_log(DEBUG, "[TIMER] Setting timer for 100ms (%llu ticks)\n", timeout_ticks);

    // Enable interrupts globally
    enable_interrupts();

    // Set timer value and enable
    write_cntp_tval_el0(timeout_ticks);
    write_cntp_ctl_el0(CNTP_CTL_ENABLE);

    tiny_log(DEBUG, "[TIMER] Timer started, waiting for interrupt...\n");

    // Wait for interrupt (simple polling with timeout)
    uint64_t start_time = read_cntpct_el0();
    uint64_t max_wait = freq; // 1 second max wait

    while (timer_interrupt_count == initial_count)
    {
        uint64_t current_time = read_cntpct_el0();
        if ((current_time - start_time) > max_wait)
        {
            tiny_log(WARN, "[TIMER] Timeout waiting for timer interrupt\n");
            // Disable timer
            write_cntp_ctl_el0(CNTP_CTL_IMASK);
            return false;
        }
    }

    tiny_log(DEBUG, "[TIMER] Timer interrupt received! Count: %d\n", timer_interrupt_count);
    tiny_log(DEBUG, "[TIMER] Simple timer test PASSED\n");

    return true;
}

uint64_t timer_get_frequency(void)
{
    return read_cntfrq_el0();
}

uint64_t timer_get_timestamp(void)
{
    return read_cntpct_el0();
}
