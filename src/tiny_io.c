/*
 * tiny_io.c
 *
 * Created on: 2025-07-21
 * Author: Debin
 *
 * Description: This file contains the implementation of I/O functions for the ARM Tiny project.
 */

#include "tiny_types.h"
#include "tiny_io.h"
#include "tiny_io.h"
#include <spin_lock.h>
#include <config.h>

spinlock_t lock;

void tiny_io_init()
{
    spinlock_init(&lock);
}

void uart_putchar(char c)
{
    volatile unsigned int *const UART0DR = (unsigned int *)UART_BASE_ADDR;
    spin_lock(&lock);
    *UART0DR = (unsigned int)c;
    spin_unlock(&lock);
}

void uart_putchar_nonlock(char c)
{
    volatile unsigned int *const UART0DR = (unsigned int *)UART_BASE_ADDR;
    *UART0DR = (unsigned int)c;
}

void print_char(char c)
{
    uart_putchar_nonlock(c);
}

void _putchar(char character)
{
    print_char(character);
}

void print_str(const char *str)
{
    while (*str)
    {
        print_char(*str++);
    }
}

// 联合体用于处理不同类型的参数

// 简单的整数转字符串
void itoa_simple(int num, char *str, int base)
{
    int i = 0;
    int isNegative = 0;

    if (num == 0)
    {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0 && base == 10)
    {
        isNegative = 1;
        num = -num;
    }

    while (num != 0)
    {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    if (isNegative)
        str[i++] = '-';

    str[i] = '\0';

    // 反转字符串
    int start = 0;
    int end = i - 1;
    while (start < end)
    {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

// 无符号整数转字符串
void utoa_simple(unsigned int num, char *str, int base)
{
    int i = 0;

    if (num == 0)
    {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    while (num != 0)
    {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    str[i] = '\0';

    // 反转字符串
    int start = 0;
    int end = i - 1;
    while (start < end)
    {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

// 扩展的格式化处理函数
void format_and_print_extended(const char *format, printf_arg_t args[], int arg_count)
{
    char buffer[512];
    const char *p = format;
    int arg_index = 0;

    while (*p)
    {
        if (*p == '%' && *(p + 1) && arg_index < arg_count)
        {
            p++; // 跳过 '%'
            if (*p >= '1' && *p <= '9')
            {
                p++;
            }

            switch (*p)
            {
            case 'd':
            {
                itoa_simple(args[arg_index].i, buffer, 10);
                print_str(buffer);
                arg_index++;
                break;
            }
            case 'u':
            {
                utoa_simple(args[arg_index].u, buffer, 10);
                print_str(buffer);
                arg_index++;
                break;
            }
            case 'x':
            {
                utoa_simple(args[arg_index].u, buffer, 16);
                print_str(buffer);
                arg_index++;
                break;
            }
            case 'X':
            {
                utoa_simple(args[arg_index].u, buffer, 16);
                // 转换为大写
                for (int i = 0; buffer[i]; i++)
                {
                    if (buffer[i] >= 'a' && buffer[i] <= 'f')
                    {
                        buffer[i] = buffer[i] - 'a' + 'A';
                    }
                }
                print_str(buffer);
                arg_index++;
                break;
            }
            case 'o':
            {
                utoa_simple(args[arg_index].u, buffer, 8);
                print_str(buffer);
                arg_index++;
                break;
            }
            case 's':
            {
                if (args[arg_index].s)
                {
                    print_str(args[arg_index].s);
                }
                else
                {
                    print_str("(null)");
                }
                arg_index++;
                break;
            }
            case 'c':
            {
                print_char(args[arg_index].c);
                arg_index++;
                break;
            }
            case 'p':
            {
                print_str("0x");
                utoa_simple(args[arg_index].u, buffer, 16);
                print_str(buffer);
                arg_index++;
                break;
            }
            case '%':
            {
                print_char('%');
                break;
            }
            default:
            {
                print_char('%');
                print_char(*p);
                break;
            }
            }
            p++;
        }
        else
        {
            print_char(*p++);
        }
    }
}

void soft_delay(int n)
{
    for (int i = 0; i < n; i++)
    {
        asm volatile("nop");
    }
}

void soft_delay_ms(int n)
{
    for (int i = 0; i < n; i++)
    {
        soft_delay(1000000);
    }
}

// 使用示例
void test_extended_printf()
{
    unsigned int uval = 0xDEADBEEF;
    uint16_t addr = 0x01;
    void *ptr = (void *)0x12345678;

    tiny_log(TRACE, "Testing extended printf:\n");

    tiny_log(TRACE, "Hello, World!\n");
    tiny_log(DEBUG, "Unsigned: %u\n", uval);
    tiny_log(WARN, "Hex upper: %X\n", uval);
    tiny_log(ERROR, "Hex lower: %2x\n", addr);
    tiny_log(DEBUG, "Pointer: %p\n", ptr);
    tiny_log(DEBUG, "Mixed: %d %u\n", -42, uval);
    tiny_log(DEBUG, "All types: %d %s %c\n", 100, "test", 'A');
}