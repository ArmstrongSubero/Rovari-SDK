/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 */

/**
 * @file rovari_uart.c
 * @brief UART print/read helpers for Baochip-1x.
 *
 * Wraps the Dabao SDK uart_putc/uart_puts/uart_getc/uart_is_readable
 * to provide the standard Rovari UART API (uart_print, uart_println,
 * uart_printf, uart_read_line, etc.).
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include "hardware/uart.h"
#include "rovari_uart.h"

#define PRINTF_BUF_SIZE 128

void uart_write_byte(UartInstance inst, uint8_t byte)
{
    uart_putc((uint)inst, (char)byte);
}

void uart_print(UartInstance inst, const char* str)
{
    if (str == (void*)0) return;
    uart_puts((uint)inst, str);
}

void uart_println(UartInstance inst, const char* str)
{
    if (str != (void*)0) {
        uart_puts((uint)inst, str);
    }
    uart_putc((uint)inst, '\r');
    uart_putc((uint)inst, '\n');
}

void uart_printf(UartInstance inst, const char* fmt, ...)
{
    char buf[PRINTF_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uart_puts((uint)inst, buf);
}

uint8_t uart_read_byte(UartInstance inst)
{
    return (uint8_t)uart_getc((uint)inst);
}

uint8_t uart_available(UartInstance inst)
{
    return uart_is_readable((uint)inst) ? 1 : 0;
}

int uart_read_line(UartInstance inst, char* buf, int max_len)
{
    if (buf == (void*)0 || max_len <= 0) return 0;

    int count = 0;
    while (count < max_len - 1)
    {
        if (!uart_is_readable((uint)inst)) break;

        char c = uart_getc((uint)inst);
        if (c == '\r' || c == '\n')
        {
            if (count > 0) break;
            continue;
        }
        buf[count++] = c;
    }
    buf[count] = '\0';
    return count;
}