#ifndef __IO_H__
#define __IO_H__

#include "tiny_types.h"
#include "printf.h"

// 配置打印日志
// LOG_LEVEL is defined by Makefile: 0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG
#ifndef LOG_LEVEL
#define LOG_LEVEL 3 // Default to INFO level
#endif

typedef enum
{
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    TRACE = 5,
} log_level_t;

typedef union
{
    int i;
    unsigned int u;
    long l;
    unsigned long ul;
    char c;
    const char *s;
    void *p;
    uintptr_t ptr;
} printf_arg_t;

static inline uint8_t read8(const volatile void *addr)
{
    return *(const volatile uint8_t *)addr;
}

static inline void write8(uint8_t value, volatile void *addr)
{
    *(volatile uint8_t *)addr = value;
}

static inline uint16_t read16(const volatile void *addr)
{
    return *(const volatile uint16_t *)addr;
}

static inline void write16(uint16_t value, volatile void *addr)
{
    *(volatile uint16_t *)addr = value;
}

static inline uint32_t read32(const volatile void *addr)
{
    return *(const volatile uint32_t *)addr;
}

static inline void write32(uint32_t value, volatile void *addr)
{
    *(volatile uint32_t *)addr = value;
}

static inline uint64_t read64(const volatile void *addr)
{
    return *(const volatile uint64_t *)addr;
}

static inline void write64(uint64_t value, volatile void *addr)
{
    *(volatile uint64_t *)addr = value;
}

void tiny_io_init();
void format_and_print_extended(const char *format, printf_arg_t args[], int arg_count);
void soft_delay_ms(int n);
void print_str(const char *str);
void print_char(char c);

#define printf_ext0(fmt) format_and_print_extended(fmt, NULL, 0)

#define printf_ext1(fmt, a1)                              \
    do                                                    \
    {                                                     \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}}; \
        format_and_print_extended(fmt, args, 1);          \
    } while (0)

#define printf_ext2(fmt, a1, a2)                                                    \
    do                                                                              \
    {                                                                               \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}, {.ptr = (uintptr_t)(a2)}}; \
        format_and_print_extended(fmt, args, 2);                                    \
    } while (0)

#define printf_ext3(fmt, a1, a2, a3)                                                                          \
    do                                                                                                        \
    {                                                                                                         \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}, {.ptr = (uintptr_t)(a2)}, {.ptr = (uintptr_t)(a3)}}; \
        format_and_print_extended(fmt, args, 3);                                                              \
    } while (0)

#define printf_ext4(fmt, a1, a2, a3, a4)                                                                                                \
    do                                                                                                                                  \
    {                                                                                                                                   \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}, {.ptr = (uintptr_t)(a2)}, {.ptr = (uintptr_t)(a3)}, {.ptr = (uintptr_t)(a4)}}; \
        format_and_print_extended(fmt, args, 4);                                                                                        \
    } while (0)

#define printf_ext5(fmt, a1, a2, a3, a4, a5)                                                                                                                      \
    do                                                                                                                                                            \
    {                                                                                                                                                             \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}, {.ptr = (uintptr_t)(a2)}, {.ptr = (uintptr_t)(a3)}, {.ptr = (uintptr_t)(a4)}, {.ptr = (uintptr_t)(a5)}}; \
        format_and_print_extended(fmt, args, 5);                                                                                                                  \
    } while (0)

#define printf_ext6(fmt, a1, a2, a3, a4, a5, a6)                                                                                                                                            \
    do                                                                                                                                                                                      \
    {                                                                                                                                                                                       \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}, {.ptr = (uintptr_t)(a2)}, {.ptr = (uintptr_t)(a3)}, {.ptr = (uintptr_t)(a4)}, {.ptr = (uintptr_t)(a5)}, {.ptr = (uintptr_t)(a6)}}; \
        format_and_print_extended(fmt, args, 6);                                                                                                                                            \
    } while (0)

#define printf_ext7(fmt, a1, a2, a3, a4, a5, a6, a7)                                                                                                                                                                  \
    do                                                                                                                                                                                                                \
    {                                                                                                                                                                                                                 \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}, {.ptr = (uintptr_t)(a2)}, {.ptr = (uintptr_t)(a3)}, {.ptr = (uintptr_t)(a4)}, {.ptr = (uintptr_t)(a5)}, {.ptr = (uintptr_t)(a6)}, {.ptr = (uintptr_t)(a7)}}; \
        format_and_print_extended(fmt, args, 7);                                                                                                                                                                      \
    } while (0)
#define printf_ext8(fmt, a1, a2, a3, a4, a5, a6, a7, a8)                                                                                                                                                                                        \
    do                                                                                                                                                                                                                                          \
    {                                                                                                                                                                                                                                           \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}, {.ptr = (uintptr_t)(a2)}, {.ptr = (uintptr_t)(a3)}, {.ptr = (uintptr_t)(a4)}, {.ptr = (uintptr_t)(a5)}, {.ptr = (uintptr_t)(a6)}, {.ptr = (uintptr_t)(a7)}, {.ptr = (uintptr_t)(a8)}}; \
        format_and_print_extended(fmt, args, 8);                                                                                                                                                                                                \
    } while (0)
#define printf_ext9(fmt, a1, a2, a3, a4, a5, a6, a7, a8, a9)                                                                                                                                                                                                              \
    do                                                                                                                                                                                                                                                                    \
    {                                                                                                                                                                                                                                                                     \
        printf_arg_t args[] = {{.ptr = (uintptr_t)(a1)}, {.ptr = (uintptr_t)(a2)}, {.ptr = (uintptr_t)(a3)}, {.ptr = (uintptr_t)(a4)}, {.ptr = (uintptr_t)(a5)}, {.ptr = (uintptr_t)(a6)}, {.ptr = (uintptr_t)(a7)}, {.ptr = (uintptr_t)(a8)}, {.ptr = (uintptr_t)(a9)}}; \
        format_and_print_extended(fmt, args, 9);                                                                                                                                                                                                                          \
    } while (0)

#define GET_MACRO(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, NAME, ...) NAME

#define printf_ext(...) GET_MACRO(_0, ##__VA_ARGS__, \
                                  printf_ext8, printf_ext7, printf_ext6, printf_ext5, printf_ext4, printf_ext3, printf_ext2, printf_ext1, printf_ext0)(__VA_ARGS__)

#define set_color(level)            \
    do                              \
    {                               \
        switch (level)              \
        {                           \
        case INFO:                  \
            printf_ext("\033[32m"); \
            break;                  \
        case WARN:                  \
            printf_ext("\033[33m"); \
            break;                  \
        case DEBUG:                 \
            printf_ext("\033[34m"); \
            break;                  \
        case ERROR:                 \
            printf_ext("\033[31m"); \
            break;                  \
        case TRACE:                 \
            printf_ext("\033[35m"); \
            break;                  \
        default:                    \
            printf_ext("\033[0m");  \
            break;                  \
        }                           \
    } while (0)
#define reset_color() printf_ext("\033[0m")

#define tiny_log_base(level, file, line, format, ...) \
    do                                                \
    {                                                 \
        printf_ext("[%s:%d] ", file, line);           \
        switch (level)                                \
        {                                             \
        case INFO:                                    \
            printf_ext("\033[32m");                   \
            break;                                    \
        case WARN:                                    \
            printf_ext("\033[33m");                   \
            break;                                    \
        case DEBUG:                                   \
            printf_ext("\033[34m");                   \
            break;                                    \
        case ERROR:                                   \
            printf_ext("\033[31m");                   \
            break;                                    \
        case TRACE:                                   \
            printf_ext("\033[35m");                   \
            break;                                    \
        default:                                      \
            printf_ext("\033[0m");                    \
            break;                                    \
        }                                             \
        printf_ext(format, ##__VA_ARGS__);            \
        printf_ext("\033[0m");                        \
    } while (0)

// 显示行号和文件名，支持编译时日志等级过滤
#define tiny_log(level, format, ...)                \
    do                                              \
    {                                               \
        if ((level) <= LOG_LEVEL)                   \
        {                                           \
            printf("[%s:%d] ", __FILE__, __LINE__); \
            set_color(level);                       \
            printf(format, ##__VA_ARGS__);          \
            reset_color();                          \
        }                                           \
    } while (0)
// tiny_log_base(level, __FILE__, __LINE__, format, ##__VA_ARGS__)

// PSCI function IDs for system shutdown
#define PSCI_SYSTEM_OFF 0x84000008

// ARM64 system shutdown function using PSCI
static inline void system_shutdown(void)
{
    tiny_log(DEBUG, "Shutting down system...\n");

    // PSCI call to shutdown the system
    __asm__ volatile(
        "mov x0, %0\n\t"
        "hvc #0\n\t"
        :
        : "r"(PSCI_SYSTEM_OFF)
        : "x0");

    // If PSCI fails, try alternative method
    // Some QEMU versions support writing to this address for shutdown
    write32(0x2000, (volatile void *)0x84000008);

    // Final fallback - infinite loop
    while (1)
    {
        __asm__ volatile("wfi"); // Wait for interrupt
    }
}

#endif // __IO_H__
