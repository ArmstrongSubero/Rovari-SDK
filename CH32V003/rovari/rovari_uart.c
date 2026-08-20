/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_uart.c
 * @brief Buffered UART for CH32V003 (USART1 only).
 *
 * USART1 on APB2. Default pins: TX=PD5, RX=PD6 (no remap).
 * 8N1 framing. TXE/RXNE polling is bounded.
 *
 * RAM budget: 64-byte RX ring + 80-byte printf buffer on stack.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_uart.h"
#include "rovari_gpio.h"

#define UART_TIMEOUT  500000U

/* Receive ring buffer - 64 bytes to fit in 2K RAM */
#define RX_BUF_SIZE 64

typedef struct {
    volatile uint8_t  buf[RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} rx_ring_t;

static rx_ring_t rx_ring1;

static inline void ring_push(rx_ring_t* r, uint8_t byte)
{
    SEVS_INVARIANT(r != NULL);
    uint16_t next = (uint16_t)((r->head + 1) % RX_BUF_SIZE);
    if (next != r->tail) {
        r->buf[r->head] = byte;
        r->head = next;
    }
}

static inline int ring_pop(rx_ring_t* r, uint8_t* byte)
{
    SEVS_INVARIANT(r != NULL);
    if (r->head == r->tail) {
        return 0;
    }
    *byte = r->buf[r->tail];
    r->tail = (uint16_t)((r->tail + 1) % RX_BUF_SIZE);
    return 1;
}

/* -----------------------------------------------------------------------
 *  Public C API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize USART1 at the given baud (8N1).
 * @req REQ-ROVARI-UART-0010
 */
void uart_init(UartInstance inst, uint32_t baud)
{
    (void)inst;  /* Only SERIAL1 exists on CH32V003 */

    /* Enable USART1 + GPIOD clocks (both APB2) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOD, ENABLE);

    /* TX = PD5 as AF push-pull */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = GPIO_Pin_5;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOD, &gpio);

    /* RX = PD6 as floating input */
    gpio.GPIO_Pin   = GPIO_Pin_6;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOD, &gpio);

    USART_InitTypeDef usart = {0};
    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);

    rx_ring1.head = 0;
    rx_ring1.tail = 0;
}

/**
 * @brief Transmit one byte (bounded wait for TX-empty).
 * @req REQ-ROVARI-UART-0011
 * @req REQ-ROVARI-UART-0020
 */
void uart_write_byte(UartInstance inst, uint8_t byte)
{
    (void)inst;
    for (uint32_t i = 0U; i < UART_TIMEOUT; i++) {
        if (USART_GetFlagStatus(USART1, USART_FLAG_TXE) != RESET) {
            USART_SendData(USART1, byte);
            return;
        }
    }
}

/**
 * @brief Transmit a null-terminated string.
 * @req REQ-ROVARI-UART-0011
 */
void uart_print(UartInstance inst, const char* str)
{
    SEVS_REQUIRE_NOT_NULL(str);
    while (*str) {
        uart_write_byte(inst, (uint8_t)*str++);
    }
}

/**
 * @brief Transmit a string followed by CRLF.
 * @req REQ-ROVARI-UART-0011
 */
void uart_println(UartInstance inst, const char* str)
{
    SEVS_REQUIRE_NOT_NULL(str);
    uart_print(inst, str);
    uart_write_byte(inst, '\r');
    uart_write_byte(inst, '\n');
}

/**
 * @brief Formatted print (bounded 80-byte buffer for 2K RAM).
 * @req REQ-ROVARI-UART-0012
 */
void uart_printf(UartInstance inst, const char* fmt, ...)
{
    SEVS_REQUIRE_NOT_NULL(fmt);
    char buf[80];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_print(inst, buf);
}

/**
 * @brief Receive one byte (bounded wait for RX-not-empty).
 * @return Byte received, or 0 on timeout.
 * @req REQ-ROVARI-UART-0013
 */
uint8_t uart_read_byte(UartInstance inst)
{
    (void)inst;
    for (uint32_t i = 0U; i < UART_TIMEOUT; i++) {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
            return (uint8_t)USART_ReceiveData(USART1);
        }
    }
    return 0;
}

/**
 * @brief Report whether a received byte is available.
 * @req REQ-ROVARI-UART-0013
 */
uint8_t uart_available(UartInstance inst)
{
    (void)inst;
    return (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) ? 1 : 0;
}

/**
 * @brief Assemble a CR/LF-terminated line from the receive ring buffer.
 * @req REQ-ROVARI-UART-0014
 */
int uart_read_line(UartInstance inst, char* buf, int max_len)
{
    (void)inst;
    SEVS_REQUIRE_NOT_NULL(buf);
    rx_ring_t* ring = &rx_ring1;

    /* Drain available hardware bytes into the ring */
    for (uint16_t i = 0U; i < RX_BUF_SIZE; i++) {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET) {
            break;
        }
        uint8_t byte = (uint8_t)USART_ReceiveData(USART1);
        ring_push(ring, byte);
    }

    /* Scan ring for a line ending (\r or \n) */
    uint16_t scan = ring->tail;
    int found = 0;
    int len = 0;
    while (scan != ring->head) {
        uint8_t ch = ring->buf[scan];
        if (ch == '\n' || ch == '\r') {
            found = 1;
            break;
        }
        len++;
        scan = (uint16_t)((scan + 1) % RX_BUF_SIZE);
    }

    if (!found) {
        return 0;
    }

    int out = 0;
    for (int i = 0; i < len && out < (max_len - 1); i++) {
        uint8_t byte = 0;
        (void)ring_pop(ring, &byte);
        buf[out++] = (char)byte;
    }
    buf[out] = '\0';

    /* Pop the line terminator, and a second one if \r\n pair */
    uint8_t term;
    (void)ring_pop(ring, &term);
    if (ring->head != ring->tail) {
        uint8_t next = ring->buf[ring->tail];
        if ((term == '\r' && next == '\n') || (term == '\n' && next == '\r')) {
            (void)ring_pop(ring, &next);
        }
    }

    return out;
}
