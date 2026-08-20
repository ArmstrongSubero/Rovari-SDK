/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_gpio.c
 * @brief GPIO configuration and digital I/O for CH32V307.
 *
 * Wraps the WCH HAL for ports A-E. Port clocks are enabled lazily, once
 * per port, on first use of a pin in that port.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_gpio.h"

/* Port base address lookup table */
static GPIO_TypeDef* const port_table[] = {
    GPIOA,  /* 0 */
    GPIOB,  /* 1 */
    GPIOC,  /* 2 */
    GPIOD,  /* 3 */
    GPIOE,  /* 4 */
};

/* RCC peripheral clock lookup table */
static const uint32_t rcc_table[] = {
    RCC_APB2Periph_GPIOA,  /* 0 */
    RCC_APB2Periph_GPIOB,  /* 1 */
    RCC_APB2Periph_GPIOC,  /* 2 */
    RCC_APB2Periph_GPIOD,  /* 3 */
    RCC_APB2Periph_GPIOE,  /* 4 */
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
    if (idx >= NUM_PORTS) {
        return GPIOA;  /* fallback safety */
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
    if (idx >= NUM_PORTS) {
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
 * @param[in] pin   Encoded pin identifier (PA0..PE15).
 * @param[in] mode  Requested pin mode.
 * @req REQ-ROVARI-GPIO-0010
 */
void pin_mode(pin_t pin, PinMode mode)
{
    SEVS_INVARIANT(ROVARI_PORT(pin) < NUM_PORTS);
    ensure_port_clock(pin);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = ROVARI_PIN_MASK(pin);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

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
 * @param[in] pin   Encoded pin identifier.
 * @param[in] value Non-zero drives high; zero drives low.
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
 * @param[in] pin Encoded pin identifier.
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
 * @param[in] pin Encoded pin identifier.
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
