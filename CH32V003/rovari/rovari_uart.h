/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_uart.h - UART abstraction for CH32V003 (USART1 only)
 */

#ifndef ROVARI_UART_H
#define ROVARI_UART_H

#include "rovari_defs.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void uart_init(UartInstance inst, uint32_t baud);
void uart_write_byte(UartInstance inst, uint8_t byte);
void uart_print(UartInstance inst, const char* str);
void uart_println(UartInstance inst, const char* str);
void uart_printf(UartInstance inst, const char* fmt, ...);
uint8_t uart_read_byte(UartInstance inst);
uint8_t uart_available(UartInstance inst);
int uart_read_line(UartInstance inst, char* buf, int max_len);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Uart {
public:
    Uart(UartInstance inst, uint32_t baud) : _inst(inst) {
        uart_init(inst, baud);
    }

    void writeByte(uint8_t b)         { uart_write_byte(_inst, b); }
    void print(const char* s)         { uart_print(_inst, s); }
    void println(const char* s)       { uart_println(_inst, s); }
    uint8_t readByte()                { return uart_read_byte(_inst); }
    uint8_t available()               { return uart_available(_inst); }
    int readLine(char* buf, int len)  { return uart_read_line(_inst, buf, len); }

private:
    UartInstance _inst;
};

#endif

#endif /* ROVARI_UART_H */
