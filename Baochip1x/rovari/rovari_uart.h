/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 *
 * rovari_uart.h - UART abstraction for Baochip-1x
 *
 * The Dabao SDK already provides uart_init(instance, baud) via
 * hardware/uart.h (included through bao.h). This header adds
 * the print/read helpers that match the CH32V003 Rovari API.
 *
 * UART mapping on Dabao board:
 *   SERIAL1 = UART0
 *   SERIAL2 = UART1
 *   SERIAL3 = UART2 (boot console, PB13=RX, PB14=TX)
 *   SERIAL4 = UART3
 */

#ifndef ROVARI_UART_H
#define ROVARI_UART_H

#include "rovari_defs.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* uart_init() is provided by the Dabao SDK (hardware/uart.h).
 * Do NOT redeclare it here. */

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
        uart_init((uint)inst, baud);
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