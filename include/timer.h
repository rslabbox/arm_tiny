#ifndef __TIMER_H__
#define __TIMER_H__

#include "tiny_types.h"
#include "config.h"

// ARM Generic Timer system registers
#define CNTFRQ_EL0 "cntfrq_el0"
#define CNTP_CTL_EL0 "cntp_ctl_el0"
#define CNTP_CVAL_EL0 "cntp_cval_el0"
#define CNTP_TVAL_EL0 "cntp_tval_el0"
#define CNTPCT_EL0 "cntpct_el0"

// Timer control register bits
#define CNTP_CTL_ENABLE (1 << 0)  // Timer enable
#define CNTP_CTL_IMASK (1 << 1)   // Timer interrupt mask
#define CNTP_CTL_ISTATUS (1 << 2) // Timer condition met

// Global timer interrupt counter
extern volatile uint32_t timer_interrupt_count;

// Function declarations
void timer_init(void);
void timer_irq_handler(uint64_t *ctx);
bool timer_test_simple(void);

// Performance measurement functions
uint64_t timer_get_frequency(void);
uint64_t timer_get_timestamp(void);

#endif // __TIMER_H__
