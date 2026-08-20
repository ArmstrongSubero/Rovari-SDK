/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_uart.c - UART implementation wrapping WCH USART HAL (CH32H417)
 *
 * Confirmed working pin mapping (V3F core):
 *   SERIAL1: TX=PA9,  RX=PA10  (USART1, on HB2)  AF7
 *   SERIAL2: TX=PA2,  RX=PA3   (USART2, on HB1)  AF7
 *   SERIAL3: TX=PD8,  RX=PD9   (USART3, on HB1)  AF7
 *
 * Note: USART4-8 have a peripheral clock mismatch on V3F that
 * prevents correct baud rate generation. To be investigated.
 */

#include "rovari_uart.h"
#include "rovari_gpio.h"
#include "debug.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* -- Instance lookup ------------------------------------------------ */
typedef struct {
    USART_TypeDef* periph;
    uint32_t       rcc_periph;   /* RCC clock bit for the USART */
    uint8_t        rcc_bus;      /* 1 = HB1, 2 = HB2 */
    pin_t          tx_pin;
    pin_t          rx_pin;
    uint8_t        tx_af;        /* AF number for TX pin */
    uint8_t        rx_af;        /* AF number for RX pin */
    uint8_t        tx_pin_source;
    uint8_t        rx_pin_source;
    GPIO_TypeDef*  tx_port;
    GPIO_TypeDef*  rx_port;
    uint32_t       tx_port_rcc;
    uint32_t       rx_port_rcc;
} UartDef;

static const UartDef uart_defs[] = {
    [0] = {0},  /* unused - instances are 1-indexed */
    [1] = { USART1, RCC_HB2Periph_USART1, 2,
            PA9, PA10, GPIO_AF7, GPIO_AF7,
            GPIO_PinSource9, GPIO_PinSource10,
            GPIOA, GPIOA, RCC_HB2Periph_GPIOA, RCC_HB2Periph_GPIOA },
    [2] = { USART2, RCC_HB1Periph_USART2, 1,
            PA2, PA3, GPIO_AF7, GPIO_AF7,
            GPIO_PinSource2, GPIO_PinSource3,
            GPIOA, GPIOA, RCC_HB2Periph_GPIOA, RCC_HB2Periph_GPIOA },
    [3] = { USART3, RCC_HB1Periph_USART3, 1,
            PD8, PD9, GPIO_AF7, GPIO_AF7,
            GPIO_PinSource8, GPIO_PinSource9,
            GPIOD, GPIOD, RCC_HB2Periph_GPIOD, RCC_HB2Periph_GPIOD },
};

#define UART_DEF_COUNT  (sizeof(uart_defs) / sizeof(uart_defs[0]))

static inline const UartDef* get_def(UartInstance inst)
{
    if (inst == 0 || inst >= UART_DEF_COUNT) return &uart_defs[1];
    return &uart_defs[inst];
}

/* -- Per-instance receive ring buffer ------------------------------- */
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
    if (next != r->tail) {
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

/* ===================================================================
 *  Public C API
 * =================================================================== */

void uart_init(UartInstance inst, uint32_t baud)
{
    const UartDef* def = get_def(inst);

    /* Enable AFIO clock (required for GPIO_PinAFConfig) */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);

    /* Enable USART clock */
    if (def->rcc_bus == 2) {
        RCC_HB2PeriphClockCmd(def->rcc_periph, ENABLE);
    } else {
        RCC_HB1PeriphClockCmd(def->rcc_periph, ENABLE);
    }

    /* Enable GPIO port clocks */
    RCC_HB2PeriphClockCmd(def->tx_port_rcc | def->rx_port_rcc, ENABLE);

    /* Configure TX pin: AF push-pull */
    GPIO_PinAFConfig(def->tx_port, def->tx_pin_source, def->tx_af);
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.GPIO_Pin   = (uint16_t)(1U << def->tx_pin_source);
        gpio.GPIO_Speed = GPIO_Speed_Very_High;
        gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
        GPIO_Init(def->tx_port, &gpio);
    }

    /* Configure RX pin: floating input */
    GPIO_PinAFConfig(def->rx_port, def->rx_pin_source, def->rx_af);
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.GPIO_Pin  = (uint16_t)(1U << def->rx_pin_source);
        gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(def->rx_port, &gpio);
    }

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
    return (USART_GetFlagStatus(get_def(inst)->periph, USART_FLAG_RXNE) != RESET) ? 1 : 0;
}

int uart_read_line(UartInstance inst, char* buf, int max_len)
{
    USART_TypeDef* periph = get_def(inst)->periph;
    RxRing* ring = &rx_rings[inst];

    while (USART_GetFlagStatus(periph, USART_FLAG_RXNE) != RESET) {
        uint8_t byte = (uint8_t)USART_ReceiveData(periph);
        ring_push(ring, byte);
    }

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

    int out = 0;
    for (int i = 0; i < len && out < (max_len - 1); i++) {
        uint8_t byte = 0;
        ring_pop(ring, &byte);
        if (byte != '\r') {
            buf[out++] = (char)byte;
        }
    }
    buf[out] = '\0';

    uint8_t nl;
    ring_pop(ring, &nl);

    return out;
}
