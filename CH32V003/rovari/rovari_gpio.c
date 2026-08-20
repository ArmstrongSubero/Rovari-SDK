/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_gpio.c
 * @brief GPIO configuration and digital I/O for CH32V003.
 *
 * Wraps the WCH HAL for ports A, C, D. Port B does not exist on this chip.
 * Port clocks are enabled lazily, once per port, on first use.
 *
 * Port index mapping (matches ROVARI_PORT encoding):
 *   0 = GPIOA, 1 = (none), 2 = GPIOC, 3 = GPIOD
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_gpio.h"

/* Port base address lookup table (sparse: index 1 is NULL, no GPIOB) */
static GPIO_TypeDef* const port_table[] = {
    GPIOA,  /* 0 */
    NULL,   /* 1 - no GPIOB on CH32V003 */
    GPIOC,  /* 2 */
    GPIOD,  /* 3 */
};

/* RCC peripheral clock lookup table */
static const uint32_t rcc_table[] = {
    RCC_APB2Periph_GPIOA,  /* 0 */
    0,                      /* 1 - no GPIOB */
    RCC_APB2Periph_GPIOC,  /* 2 */
    RCC_APB2Periph_GPIOD,  /* 3 */
};

#define NUM_PORTS  (sizeof(port_table) / sizeof(port_table[0]))

/* Track which port clocks have been enabled (bit per port) */
static uint8_t s_port_clocks_enabled = 0;

/**
 * @brief Resolve a pin to its GPIO port base, bounded to valid ports.
 * @req REQ-ROVARI-GPIO-0020
 */
static GPIO_TypeDef* rovari_get_port(pin_t pin)
{
    uint8_t idx = ROVARI_PORT(pin);
    if (idx >= NUM_PORTS || port_table[idx] == NULL) {
        return GPIOD;  /* fallback to port D (always present) */
    }
    return port_table[idx];
}

/**
 * @brief Enable a pin's port clock exactly once per port.
 * @req REQ-ROVARI-GPIO-0010
 */
static void ensure_port_clock(pin_t pin)
{
    uint8_t idx = ROVARI_PORT(pin);
    if (idx >= NUM_PORTS || port_table[idx] == NULL) {
        return;
    }

    uint8_t mask = (uint8_t)(1U << idx);
    if (!(s_port_clocks_enabled & mask)) {
        RCC_APB2PeriphClockCmd(rcc_table[idx], ENABLE);
        s_port_clocks_enabled |= mask;
    }
}

/* -----------------------------------------------------------------------
 *  Public C API
 * ----------------------------------------------------------------------- */

/**
 * @brief Configure a pin's direction and drive/pull mode.
 * @param[in] pin   Encoded pin identifier (PA1..PD7).
 * @param[in] mode  Requested pin mode.
 * @req REQ-ROVARI-GPIO-0010
 */
void pin_mode(pin_t pin, PinMode mode)
{
    uint8_t idx = ROVARI_PORT(pin);
    SEVS_INVARIANT(idx < NUM_PORTS && port_table[idx] != NULL);
    ensure_port_clock(pin);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = ROVARI_PIN_MASK(pin);
    gpio.GPIO_Speed = GPIO_Speed_30MHz;

    switch (mode) {
        case Output:        gpio.GPIO_Mode = GPIO_Mode_Out_PP;      break;
        case Input:         gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING; break;
        case InputPullUp:   gpio.GPIO_Mode = GPIO_Mode_IPU;         break;
        case InputPullDown: gpio.GPIO_Mode = GPIO_Mode_IPD;         break;
        case OutputOD:      gpio.GPIO_Mode = GPIO_Mode_Out_OD;      break;
        case AF_PushPull:   gpio.GPIO_Mode = GPIO_Mode_AF_PP;       break;
        case AF_OpenDrain:  gpio.GPIO_Mode = GPIO_Mode_AF_OD;       break;
        case Analog:        gpio.GPIO_Mode = GPIO_Mode_AIN;         break;
        default:            gpio.GPIO_Mode = GPIO_Mode_Out_PP;      break;
    }

    GPIO_Init(rovari_get_port(pin), &gpio);
}

/**
 * @brief Drive a pin high or low.
 * @req REQ-ROVARI-GPIO-0011
 */
void digital_write(pin_t pin, uint8_t value)
{
    GPIO_TypeDef* port = rovari_get_port(pin);
    SEVS_INVARIANT(port != NULL);
    uint16_t mask = ROVARI_PIN_MASK(pin);

    if (value) {
        GPIO_SetBits(port, mask);
    } else {
        GPIO_ResetBits(port, mask);
    }
}

/**
 * @brief Read a pin's input level.
 * @return 1 if the input is high, 0 otherwise.
 * @req REQ-ROVARI-GPIO-0012
 */
uint8_t digital_read(pin_t pin)
{
    GPIO_TypeDef* port = rovari_get_port(pin);
    SEVS_INVARIANT(port != NULL);
    return GPIO_ReadInputDataBit(port, ROVARI_PIN_MASK(pin));
}

/**
 * @brief Invert a pin's output level.
 * @req REQ-ROVARI-GPIO-0013
 */
void pin_toggle(pin_t pin)
{
    GPIO_TypeDef* port = rovari_get_port(pin);
    SEVS_INVARIANT(port != NULL);
    uint16_t mask = ROVARI_PIN_MASK(pin);

    if (port->OUTDR & mask) {
        GPIO_ResetBits(port, mask);
    } else {
        GPIO_SetBits(port, mask);
    }
}
