/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_gpio.c — GPIO implementation wrapping WCH HAL
 */

#include "rovari_gpio.h"
#include "debug.h"

/* ── Port base address lookup table ─────────────────────────────────── */
static GPIO_TypeDef* const port_table[] = {
    GPIOA,  /* 0 */
    GPIOB,  /* 1 */
    GPIOC,  /* 2 */
    GPIOD,  /* 3 */
};

/* ── RCC peripheral clock lookup table ──────────────────────────────── */
static const uint32_t rcc_table[] = {
    RCC_APB2Periph_GPIOA,  /* 0 */
    RCC_APB2Periph_GPIOB,  /* 1 */
    RCC_APB2Periph_GPIOC,  /* 2 */
    RCC_APB2Periph_GPIOD,  /* 3 */
};

#define NUM_PORTS  (sizeof(port_table) / sizeof(port_table[0]))

/* Track which port clocks have been enabled (bit per port) */
static uint8_t _port_clocks_enabled = 0;

/* ── Helper: get GPIO port from pin ─────────────────────────────────── */
static GPIO_TypeDef* rovari_get_port(pin_t pin)
{
    uint8_t idx = ROVARI_PORT(pin);
    if (idx >= NUM_PORTS) return GPIOA;  /* fallback safety */
    return port_table[idx];
}

/* ── Helper: get RCC clock mask from pin ────────────────────────────── */
static uint32_t rovari_get_rcc(pin_t pin)
{
    uint8_t idx = ROVARI_PORT(pin);
    if (idx >= NUM_PORTS) return RCC_APB2Periph_GPIOA;
    return rcc_table[idx];
}

/* ── Enable port clock (only once per port) ─────────────────────────── */
static void ensure_port_clock(pin_t pin)
{
    uint8_t idx = ROVARI_PORT(pin);
    if (idx >= NUM_PORTS) return;

    uint8_t mask = (1U << idx);
    if (!(_port_clocks_enabled & mask)) {
        RCC_APB2PeriphClockCmd(rcc_table[idx], ENABLE);
        _port_clocks_enabled |= mask;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public C API
 * ═══════════════════════════════════════════════════════════════════════ */

void pin_mode(pin_t pin, PinMode mode)
{
    ensure_port_clock(pin);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = ROVARI_PIN_MASK(pin);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    switch (mode) {
        case Output:
            gpio.GPIO_Mode = GPIO_Mode_Out_PP;
            break;
        case Input:
            gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
            break;
        case InputPullUp:
            gpio.GPIO_Mode = GPIO_Mode_IPU;
            break;
        case InputPullDown:
            gpio.GPIO_Mode = GPIO_Mode_IPD;
            break;
        case OutputOD:
            gpio.GPIO_Mode = GPIO_Mode_Out_OD;
            break;
        case AF_PushPull:
            gpio.GPIO_Mode = GPIO_Mode_AF_PP;
            break;
        case AF_OpenDrain:
            gpio.GPIO_Mode = GPIO_Mode_AF_OD;
            break;
        case Analog:
            gpio.GPIO_Mode = GPIO_Mode_AIN;
            break;
        default:
            gpio.GPIO_Mode = GPIO_Mode_Out_PP;
            break;
    }

    GPIO_Init(rovari_get_port(pin), &gpio);
}

void digital_write(pin_t pin, uint8_t value)
{
    GPIO_TypeDef* port = rovari_get_port(pin);
    uint16_t mask = ROVARI_PIN_MASK(pin);

    if (value) {
        GPIO_SetBits(port, mask);
    } else {
        GPIO_ResetBits(port, mask);
    }
}

uint8_t digital_read(pin_t pin)
{
    return GPIO_ReadInputDataBit(rovari_get_port(pin), ROVARI_PIN_MASK(pin));
}

void pin_toggle(pin_t pin)
{
    GPIO_TypeDef* port = rovari_get_port(pin);
    uint16_t mask = ROVARI_PIN_MASK(pin);

    if (port->OUTDR & mask) {
        GPIO_ResetBits(port, mask);
    } else {
        GPIO_SetBits(port, mask);
    }
}
