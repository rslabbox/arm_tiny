/*
 * exception.c
 *
 * Exception handling for ARM architecture.
 *
 * This file contains the implementation of exception handling functions
 * including synchronous exceptions, IRQ exceptions, and invalid exceptions.
 * It also includes the initialization function for setting up the exception
 * handling mechanism.
 *
 * Author: Debin
 * Date: 2024-11-15
 */

#include "tiny_types.h"
#include "handle.h"
#include "tiny_io.h"
#include "gicv2.h"

irq_handler_t g_handler_vec[512] = {0};

void irq_handle_register(int vector, void (*h)(uint64_t *))
{
    g_handler_vec[vector] = h;
}

void handle_sync_exception(uint64_t *stack_pointer)
{
    trap_frame_t *el1_ctx = (trap_frame_t *)stack_pointer;

    int el1_esr = read_esr_el1();

    int ec = ((el1_esr >> 26) & 0b111111);

    tiny_log(TRACE, "el1 esr: %d\n", el1_esr);
    tiny_log(TRACE, "ec: %x\n", ec);

    tiny_log(TRACE, "This is handle_sync_exception: \n");
    for (int i = 0; i < 31; i++)
    {
        uint64_t value = el1_ctx->r[i];
        tiny_log(TRACE, "General-purpose register: %d, value: %x\n", i, value);
    }

    uint64_t elr_el1_value = el1_ctx->elr;
    uint64_t usp_value = el1_ctx->usp;
    uint64_t spsr_value = el1_ctx->spsr;

    tiny_log(TRACE, "usp: %x, elr: %x, spsr: %x\n", usp_value, elr_el1_value, spsr_value);

    while (1)
        ;
}

void handle_irq_exception(uint64_t *stack_pointer)
{
    static uint32_t irq_count = 0;
    irq_count++;

    trap_frame_t *el1_ctx = (trap_frame_t *)stack_pointer;

    tiny_log(TRACE, "[IRQ_HANDLER] *** IRQ EXCEPTION #%d TRIGGERED ***\n", irq_count);
    tiny_log(TRACE, "[IRQ_HANDLER] Stack pointer: 0x%x\n", (uint64_t)stack_pointer);

    // Read IAR and extract vector
    int iar = gic_read_iar();
    int vector = gic_iar_irqnr(iar);

    tiny_log(TRACE, "[IRQ_HANDLER] GIC IAR: 0x%x, Vector: %d\n", iar, vector);

    // Check if handler exists
    if (g_handler_vec[vector] == 0)
    {
        tiny_log(ERROR, "[IRQ_HANDLER] ERROR: No handler for IRQ %d!\n", vector);
    }
    else
    {
        tiny_log(TRACE, "[IRQ_HANDLER] Calling handler for IRQ %d\n", vector);
        g_handler_vec[vector]((uint64_t *)el1_ctx);
        tiny_log(TRACE, "[IRQ_HANDLER] Handler for IRQ %d completed\n", vector);
    }

    // Complete interrupt handling
    tiny_log(DEBUG, "[IRQ_HANDLER] Writing EOIR: 0x%x\n", iar);
    gic_write_eoir(iar);
    gic_write_dir(iar);

    tiny_log(TRACE, "[IRQ_HANDLER] IRQ exception handling completed\n");
}
void invalid_exception(uint64_t *stack_pointer, uint64_t kind, uint64_t source)
{
    tiny_log(ERROR, "Invalid exception occurred!\n");
    while (1)
        ;
}
