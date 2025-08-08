

/*   ============= gic.c ================*/

#include "gicv2.h"
#include "tiny_types.h"
#include "tiny_io.h"

struct gic_t _gicv2;

void gic_test_init(void)
{
    tiny_log(TRACE, "[guest]     gicd enable %s\n", read32((void *)GICD_CTLR) ? "ok" : "error");
    tiny_log(TRACE, "[guest]     gicc enable %s\n", read32((void *)GICC_CTLR) ? "ok" : "error");
    tiny_log(TRACE, "[guest]     irq numbers: %d\n", _gicv2.irq_nr);
    tiny_log(TRACE, "[guest]     cpu num: %d\n", cpu_num());
}

// gicd g0, g1  gicc enable
void gic_init(void)
{
    tiny_log(TRACE, "[GIC_INIT] Starting GIC initialization\n");

    _gicv2.irq_nr = GICD_TYPER_IRQS(read32((void *)GICD_TYPER));
    if (_gicv2.irq_nr > 1020)
    {
        _gicv2.irq_nr = 1020;
    }

    tiny_log(TRACE, "[GIC_INIT] IRQ count: %d\n", _gicv2.irq_nr);

    // Read current GICD_CTLR before setting
    uint32_t gicd_ctlr_before = read32((void *)GICD_CTLR);
    tiny_log(TRACE, "[GIC_INIT] GICD_CTLR before: 0x%x\n", gicd_ctlr_before);

    write32(GICD_CTRL_ENABLE_GROUP0 | GICD_CTRL_ENABLE_GROUP1, (void *)GICD_CTLR);

    // Verify GICD_CTLR was set correctly
    uint32_t gicd_ctlr_after = read32((void *)GICD_CTLR);
    tiny_log(TRACE, "[GIC_INIT] GICD_CTLR after: 0x%x (expected: 0x%x)\n",
             gicd_ctlr_after, GICD_CTRL_ENABLE_GROUP0 | GICD_CTRL_ENABLE_GROUP1);

    // Read current GICC_PMR before setting
    uint32_t gicc_pmr_before = read32((void *)GICC_PMR);
    tiny_log(TRACE, "[GIC_INIT] GICC_PMR before: 0x%x\n", gicc_pmr_before);

    // 允许所有优先级的中断
    write32(0xff - 7, (void *)GICC_PMR);

    uint32_t gicc_pmr_after = read32((void *)GICC_PMR);
    tiny_log(TRACE, "[GIC_INIT] GICC_PMR after: 0x%x (expected: 0x%x)\n",
             gicc_pmr_after, 0xff - 7);

    // Read current GICC_CTLR before setting
    uint32_t gicc_ctlr_before = read32((void *)GICC_CTLR);
    tiny_log(TRACE, "[GIC_INIT] GICC_CTLR before: 0x%x\n", gicc_ctlr_before);

    // WARNING: The (1 << 9) bit is suspicious - let's document what we're setting
    uint32_t gicc_ctlr_value = GICC_CTRL_ENABLE | (1 << 9);
    tiny_log(TRACE, "[GIC_INIT] Setting GICC_CTLR to: 0x%x (ENABLE=0x%x + bit9=0x%x)\n",
             gicc_ctlr_value, GICC_CTRL_ENABLE, (1 << 9));

    write32(gicc_ctlr_value, (void *)GICC_CTLR);

    uint32_t gicc_ctlr_after = read32((void *)GICC_CTLR);
    tiny_log(TRACE, "[GIC_INIT] GICC_CTLR after: 0x%x\n", gicc_ctlr_after);

    tiny_log(TRACE, "[GIC_INIT] GIC initialization completed\n");
    gic_test_init();
}

void gicc_init()
{
    // 允许所有优先级的中断
    write32(0xff - 7, (void *)GICC_PMR);
    write32(GICC_CTRL_ENABLE, (void *)GICC_CTLR);
}

// get iar
uint32_t gic_read_iar(void)
{
    return read32((void *)GICC_IAR);
}

// iar to vector
uint32_t gic_iar_irqnr(uint32_t iar)
{
    return iar & GICC_IAR_INT_ID_MASK;
}

void gic_write_eoir(uint32_t irqstat)
{
    write32(irqstat, (void *)GICC_EOIR);
}

void gic_write_dir(uint32_t irqstat)
{
    write32(irqstat, (void *)GICC_DIR);
}

// 发送给特定的核（某个核）
void gic_ipi_send_single(int irq, int cpu)
{
    // assert(cpu < 8);
    // assert(irq < 16);
    write32(1 << (cpu + 16) | irq, (void *)GICD_SGIR);
}

// The number of implemented CPU interfaces.
uint32_t cpu_num(void)
{
    return GICD_TYPER_CPU_NUM(read32((void *)GICD_TYPER));
}

// Enables the given interrupt.
void gic_enable_int(int vector, int pri)
{
    int reg = vector >> 5;                     //  vec / 32
    int mask = 1 << (vector & ((1 << 5) - 1)); //  vec % 32
    tiny_log(TRACE, "[guest] set enable: reg: %d, mask: 0x%x\n", reg, mask);

    write32(mask, (void *)(uint64_t)GICD_ISENABLER(reg));

    int n = vector >> 2;
    int m = vector & ((1 << 2) - 1);

    // a. 计算目标 32 位寄存器的地址
    volatile uint32_t *reg_addr = (volatile uint32_t *)((uint64_t)GICD_IPRIORITYR(n));
    // b. 读取当前寄存器的值
    uint32_t reg_val = read32(reg_addr);
    // c. 计算要清除的旧值的掩码
    uint32_t new_mask = 0xFF << (m * 8);
    // d. 计算新值
    uint32_t new_val = (pri << 3) | (1 << 7);
    // e. 将新值移位到正确位置
    new_val <<= (m * 8);
    // f. 清除旧值并设置新值
    reg_val = (reg_val & ~new_mask) | new_val;
    // g. 将最终结果写回寄存器
    write32(reg_val, reg_addr);
}

// disables the given interrupt.
void gic_disable_int(int vector, int pri)
{
    int reg = vector >> 5;                     //  vec / 32
    int mask = 1 << (vector & ((1 << 5) - 1)); //  vec % 32
    tiny_log(TRACE, "[guest] disable: reg: %d, mask: 0x%x\n", reg, mask);

    write32(mask, (void *)(uint64_t)GICD_ICENABLER(reg));
}

// check the given interrupt.
int gic_get_enable(int vector)
{
    int reg = vector >> 5;                     //  vec / 32
    int mask = 1 << (vector & ((1 << 5) - 1)); //  vec % 32

    uint32_t val = read32((void *)(uint64_t)GICD_ISENABLER(reg));

    tiny_log(TRACE, "[guest] get enable: reg: %x, mask: %x, value: %x\n", reg, mask, val);
    return (val & mask) != 0;
}

void gic_set_isenabler(uint32_t n, uint32_t value)
{
    write32(value, (void *)(uint64_t)GICD_ISENABLER(n));
}

void gic_set_ipriority(uint32_t n, uint32_t value)
{
    write32(value, (void *)(uint64_t)GICD_IPRIORITYR(n));
}

void gic_set_icenabler(uint32_t n, uint32_t value)
{
    write32(value, (void *)(uint64_t)GICD_ICENABLER(n));
}
