/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_uart.c
 * @brief Buffered UART for CH32V307 USART1-3 / UART4-8.
 *
 * SERIAL1 on APB2 (USART1), SERIAL2-8 on APB1. 8N1 framing. TXE/RXNE
 * polling is bounded so a stalled USART cannot hang the CPU.
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

/* Bounded poll cap for TXE/RXNE waits. */
#define UART_TIMEOUT  1000000U

/* Instance lookup */
typedef struct {
    USART_TypeDef* periph;
    uint32_t       rcc_apb;
    uint8_t        apb_bus;      /* 1 = APB1, 2 = APB2 */
    pin_t          tx_pin;
    pin_t          rx_pin;
} uart_def_t;

static const uart_def_t uart_defs[] = {
    [0] = {0},
    [1] = { USART1, RCC_APB2Periph_USART1, 2, PA9,  PA10 },
    [2] = { USART2, RCC_APB1Periph_USART2, 1, PA2,  PA3  },
    [3] = { USART3, RCC_APB1Periph_USART3, 1, PB10, PB11 },
    [4] = { UART4,  RCC_APB1Periph_UART4,  1, PC10, PC11 },
    [5] = { UART5,  RCC_APB1Periph_UART5,  1, PC12, PD2  },
    [6] = { UART6,  RCC_APB1Periph_UART6,  1, PC0,  PC1  },
    [7] = { UART7,  RCC_APB1Periph_UART7,  1, PC2,  PC3  },
    [8] = { UART8,  RCC_APB1Periph_UART8,  1, PC4,  PC5  },
};

#define UART_DEF_COUNT  (sizeof(uart_defs) / sizeof(uart_defs[0]))

/**
 * @brief Resolve a UART instance to its definition, bounded.
 */
static const uart_def_t* get_def(UartInstance inst)
{
    if (inst == 0 || inst >= UART_DEF_COUNT) {
        return &uart_defs[1];
    }
    return &uart_defs[inst];
}

/* Per-instance receive ring buffer */
#define RX_BUF_SIZE 128

typedef struct {
    volatile uint8_t  buf[RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} rx_ring_t;

static rx_ring_t rx_rings[UART_DEF_COUNT];

/**
 * @brief Push a byte into a receive ring, dropping it if full.
 */
static inline void ring_push(rx_ring_t* r, uint8_t byte)
{
    SEVS_INVARIANT(r != NULL);
    uint16_t next = (uint16_t)((r->head + 1) % RX_BUF_SIZE);
    if (next != r->tail) {
        r->buf[r->head] = byte;
        r->head = next;
    }
}

/**
 * @brief Pop a byte from a receive ring.
 * @return 1 if a byte was returned, 0 if the ring was empty.
 */
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
 * @brief Initialize a UART at the given baud (8N1).
 * @param[in] inst UART instance (SERIAL1-8).
 * @param[in] baud Baud rate.
 * @req REQ-ROVARI-UART-0010
 */
void uart_init(UartInstance inst, uint32_t baud)
{
    const uart_def_t* def = get_def(inst);
    SEVS_INVARIANT(inst < UART_DEF_COUNT);

    if (def->apb_bus == 2) {
        RCC_APB2PeriphClockCmd(def->rcc_apb, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(def->rcc_apb, ENABLE);
    }

    pin_mode(def->tx_pin, AF_PushPull);
    pin_mode(def->rx_pin, Input);

    USART_InitTypeDef usart = {0};
    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(def->periph, &usart);
    USART_Cmd(def->periph, ENABLE);

    rx_rings[inst].head = 0;
    rx_rings[inst].tail = 0;
}

/**
 * @brief Transmit one byte (bounded wait for TX-empty).
 * @param[in] inst UART instance.
 * @param[in] byte Byte to transmit.
 * @req REQ-ROVARI-UART-0011
 * @req REQ-ROVARI-UART-0020
 */
void uart_write_byte(UartInstance inst, uint8_t byte)
{
    USART_TypeDef* periph = get_def(inst)->periph;
    for (uint32_t i = 0U; i < UART_TIMEOUT; i++) {
        if (USART_GetFlagStatus(periph, USART_FLAG_TXE) != RESET) {
            USART_SendData(periph, byte);
            return;
        }
    }
}

/**
 * @brief Transmit a null-terminated string.
 * @param[in] inst UART instance.
 * @param[in] str  String to transmit.
 * @req REQ-ROVARI-UART-0011
 * @req REQ-ROVARI-UART-0021
 */
void uart_print(UartInstance inst, const char* str)
{
    SEVS_REQUIRE_NOT_NULL(str);
    /* @sevs-bound: terminated by the string's null terminator. */
    while (*str) {
        uart_write_byte(inst, (uint8_t)*str++);
    }
}

/**
 * @brief Transmit a string followed by CRLF.
 * @param[in] inst UART instance.
 * @param[in] str  String to transmit.
 * @req REQ-ROVARI-UART-0011
 * @req REQ-ROVARI-UART-0021
 */
void uart_println(UartInstance inst, const char* str)
{
    SEVS_REQUIRE_NOT_NULL(str);
    uart_print(inst, str);
    uart_write_byte(inst, '\r');
    uart_write_byte(inst, '\n');
}

/**
 * @brief Transmit a printf-formatted string (bounded 256-byte buffer).
 * @param[in] inst UART instance.
 * @param[in] fmt  printf-style format string.
 * @req REQ-ROVARI-UART-0012
 * @req REQ-ROVARI-UART-0021
 */
void uart_printf(UartInstance inst, const char* fmt, ...)
{
    SEVS_REQUIRE_NOT_NULL(fmt);
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_print(inst, buf);
}

/**
 * @brief Receive one byte (bounded wait for RX-not-empty).
 * @param[in] inst UART instance.
 * @return Byte received, or 0 on timeout.
 * @req REQ-ROVARI-UART-0013
 * @req REQ-ROVARI-UART-0020
 */
uint8_t uart_read_byte(UartInstance inst)
{
    USART_TypeDef* periph = get_def(inst)->periph;
    for (uint32_t i = 0U; i < UART_TIMEOUT; i++) {
        if (USART_GetFlagStatus(periph, USART_FLAG_RXNE) != RESET) {
            return (uint8_t)USART_ReceiveData(periph);
        }
    }
    return 0;  /* Timeout */
}

/**
 * @brief Report whether a received byte is available.
 * @param[in] inst UART instance.
 * @return 1 if a byte is ready, 0 otherwise.
 * @req REQ-ROVARI-UART-0013
 */
uint8_t uart_available(UartInstance inst)
{
    return (USART_GetFlagStatus(get_def(inst)->periph, USART_FLAG_RXNE) != RESET) ? 1 : 0;
}

/**
 * @brief Assemble a CR/LF-terminated line from the receive ring buffer.
 * @param[in]  inst    UART instance.
 * @param[out] buf     Destination for the line (null-terminated).
 * @param[in]  max_len Capacity of buf including the null terminator.
 * @return Line length, or 0 if no complete line is available yet.
 * @req REQ-ROVARI-UART-0014
 * @req REQ-ROVARI-UART-0021
 */
int uart_read_line(UartInstance inst, char* buf, int max_len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    USART_TypeDef* periph = get_def(inst)->periph;
    rx_ring_t* ring = &rx_rings[inst];

    /* Drain available hardware bytes into the ring (bounded by RX_BUF_SIZE). */
    for (uint16_t i = 0U; i < RX_BUF_SIZE; i++) {
        if (USART_GetFlagStatus(periph, USART_FLAG_RXNE) == RESET) {
            break;
        }
        uint8_t byte = (uint8_t)USART_ReceiveData(periph);
        ring_push(ring, byte);
    }

    /* Scan ring for a newline. */
    uint16_t scan = ring->tail;
    int found = 0;
    int len = 0;
    /* @sevs-bound: scan advances toward ring->head, at most RX_BUF_SIZE steps. */
    while (scan != ring->head) {
        if (ring->buf[scan] == '\n') {
            found = 1;
            break;
        }
        len++;
        scan = (uint16_t)((scan + 1) % RX_BUF_SIZE);
    }

    if (!found) {
        return 0;
    }

    /* Extract bytes up to (not including) the newline. */
    int out = 0;
    for (int i = 0; i < len && out < (max_len - 1); i++) {
        uint8_t byte = 0;
        (void)ring_pop(ring, &byte);
        if (byte != '\r') {
            buf[out++] = (char)byte;
        }
    }
    buf[out] = '\0';

    /* Pop the newline itself. */
    uint8_t nl;
    (void)ring_pop(ring, &nl);

    return out;
}
