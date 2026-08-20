/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_uart.c — UART implementation wrapping WCH USART HAL
 *
 * Default pin mapping (CH32V203):
 *   SERIAL1: TX=PA9,  RX=PA10  (USART1, on APB2 at 144 MHz)
 *   SERIAL2: TX=PA2,  RX=PA3   (USART2, on APB1 at 72 MHz)
 *   SERIAL3: TX=PB10, RX=PB11  (USART3, on APB1 at 72 MHz)
 *   SERIAL4: TX=PC10, RX=PC11  (UART4,  on APB1 at 72 MHz, C8/RB only)
 *
 * Baud rate calculation:
 *   The USART hardware divides the bus clock by (16 × USARTDIV) to produce
 *   the bit clock.  USARTDIV is a 16-bit fixed-point value (12.4 format)
 *   stored in the BRR register.  The WCH HAL computes this for us from
 *   the baud parameter and the current bus clock.
 *
 *   USART1 (APB2): divisor = 144 000 000 / (16 × baud)
 *   USART2–8 (APB1): divisor = 72 000 000 / (16 × baud)
 */

#include "rovari_uart.h"
#include "rovari_gpio.h"
#include "debug.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ── Instance lookup ────────────────────────────────────────────────── */
typedef struct {
    USART_TypeDef* periph;
    uint32_t       rcc_apb;      /* APB1 or APB2 clock bit */
    uint8_t        apb_bus;      /* 1 = APB1, 2 = APB2 */
    pin_t          tx_pin;
    pin_t          rx_pin;
} UartDef;

static const UartDef uart_defs[] = {
    [0] = {0},  /* unused — instances are 1-indexed */
    [1] = { USART1, RCC_APB2Periph_USART1, 2, PA9,  PA10 },
    [2] = { USART2, RCC_APB1Periph_USART2, 1, PA2,  PA3  },
    [3] = { USART3, RCC_APB1Periph_USART3, 1, PB10, PB11 },
    [4] = { UART4,  RCC_APB1Periph_UART4,  1, PC10, PC11 },
};

#define UART_DEF_COUNT  (sizeof(uart_defs) / sizeof(uart_defs[0]))

static inline const UartDef* get_def(UartInstance inst)
{
    if (inst == 0 || inst >= UART_DEF_COUNT) return &uart_defs[1];
    return &uart_defs[inst];
}

/* ── Per-instance receive ring buffer ───────────────────────────────── */
#define RX_BUF_SIZE 128

typedef struct {
    volatile uint8_t buf[RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} RxRing;

static RxRing rx_rings[UART_DEF_COUNT];

static inline void ring_push(RxRing* r, uint8_t byte)
{
    uint16_t next = (r->head + 1) % RX_BUF_SIZE;
    if (next != r->tail) {  /* drop byte if full */
        r->buf[r->head] = byte;
        r->head = next;
    }
}

static inline int ring_pop(RxRing* r, uint8_t* byte)
{
    if (r->head == r->tail) return 0;
    *byte = r->buf[r->tail];
    r->tail = (r->tail + 1) % RX_BUF_SIZE;
    return 1;
}

static inline int ring_empty(RxRing* r)
{
    return r->head == r->tail;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public C API
 * ═══════════════════════════════════════════════════════════════════════ */

void uart_init(UartInstance inst, uint32_t baud)
{
    const UartDef* def = get_def(inst);

    /* Enable USART clock */
    if (def->apb_bus == 2) {
        RCC_APB2PeriphClockCmd(def->rcc_apb, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(def->rcc_apb, ENABLE);
    }

    /* Configure TX pin as alternate-function push-pull */
    pin_mode(def->tx_pin, AF_PushPull);

    /* Configure RX pin as floating input */
    pin_mode(def->rx_pin, Input);

    /* USART configuration */
    USART_InitTypeDef usart = {0};
    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(def->periph, &usart);
    USART_Cmd(def->periph, ENABLE);

    /* Reset the receive ring buffer */
    rx_rings[inst].head = 0;
    rx_rings[inst].tail = 0;
}

void uart_write_byte(UartInstance inst, uint8_t byte)
{
    USART_TypeDef* periph = get_def(inst)->periph;
    while (USART_GetFlagStatus(periph, USART_FLAG_TXE) == RESET);
    USART_SendData(periph, byte);
}

void uart_print(UartInstance inst, const char* str)
{
    while (*str) {
        uart_write_byte(inst, (uint8_t)*str++);
    }
}

void uart_println(UartInstance inst, const char* str)
{
    uart_print(inst, str);
    uart_write_byte(inst, '\r');
    uart_write_byte(inst, '\n');
}

void uart_printf(UartInstance inst, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    uart_print(inst, buf);
}

uint8_t uart_read_byte(UartInstance inst)
{
    USART_TypeDef* periph = get_def(inst)->periph;
    while (USART_GetFlagStatus(periph, USART_FLAG_RXNE) == RESET);
    return (uint8_t)USART_ReceiveData(periph);
}

uint8_t uart_available(UartInstance inst)
{
    /* Check the hardware flag directly (polled mode) */
    return (USART_GetFlagStatus(get_def(inst)->periph, USART_FLAG_RXNE) != RESET) ? 1 : 0;
}

int uart_read_line(UartInstance inst, char* buf, int max_len)
{
    /* Drain any available bytes from hardware into the ring buffer */
    USART_TypeDef* periph = get_def(inst)->periph;
    RxRing* ring = &rx_rings[inst];

    while (USART_GetFlagStatus(periph, USART_FLAG_RXNE) != RESET) {
        uint8_t byte = (uint8_t)USART_ReceiveData(periph);
        ring_push(ring, byte);
    }

    /* Scan ring for a newline */
    uint16_t scan = ring->tail;
    int found = 0;
    int len = 0;

    while (scan != ring->head) {
        if (ring->buf[scan] == '\n') {
            found = 1;
            break;
        }
        len++;
        scan = (scan + 1) % RX_BUF_SIZE;
    }

    if (!found) return 0;

    /* Extract bytes up to (not including) the newline */
    int out = 0;
    for (int i = 0; i < len && out < (max_len - 1); i++) {
        uint8_t byte = 0;
        ring_pop(ring, &byte);
        if (byte != '\r') {          /* strip CR */
            buf[out++] = (char)byte;
        }
    }
    buf[out] = '\0';

    /* Pop the newline itself */
    uint8_t nl;
    ring_pop(ring, &nl);

    return out;
}