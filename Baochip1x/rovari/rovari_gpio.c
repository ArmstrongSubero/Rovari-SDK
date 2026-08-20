/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 */

/**
 * @file rovari_gpio.c
 * @brief GPIO configuration and digital I/O for Baochip-1x.
 *
 * Wraps the Dabao SDK IOX GPIO for ports A through F.
 * The ROVARI_PIN encoding packs port index in bits 7:4 and
 * pin number in bits 3:0, which maps directly to the Dabao
 * gpio_init(port, pin) convention.
 *
 * Note: Baochip-1x has pull-up resistors only (no pull-down).
 * InputPullDown mode falls back to floating input.
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "hardware/gpio.h"
#include "rovari_gpio.h"

#define NUM_PORTS 6

/* -----------------------------------------------------------------------
 *  Public C API
 * ----------------------------------------------------------------------- */

/**
 * @brief Configure a pin's direction and drive/pull mode.
 * @param[in] pin   Encoded pin identifier (PA0..PF15).
 * @param[in] mode  Requested pin mode.
 * @req REQ-ROVARI-GPIO-0010
 */
void pin_mode(pin_t pin, PinMode mode)
{
    uint8_t port = ROVARI_PORT(pin);
    uint8_t num  = ROVARI_PIN_NUM(pin);
    SEVS_INVARIANT(port < NUM_PORTS);

    gpio_init(port, num);

    switch (mode) {
        case Output:
        case OutputOD:
        case AF_PushPull:
        case AF_OpenDrain:
            gpio_set_dir(port, num, true);
            gpio_disable_pulls(port, num);
            break;

        case InputPullUp:
            gpio_set_dir(port, num, false);
            gpio_pull_up(port, num);
            break;

        case Input:
        case InputPullDown:
        case Analog:
        default:
            gpio_set_dir(port, num, false);
            gpio_disable_pulls(port, num);
            break;
    }
}

/**
 * @brief Drive a pin high or low.
 * @req REQ-ROVARI-GPIO-0011
 */
void digital_write(pin_t pin, uint8_t value)
{
    uint8_t port = ROVARI_PORT(pin);
    uint8_t num  = ROVARI_PIN_NUM(pin);
    SEVS_INVARIANT(port < NUM_PORTS);

    gpio_put(port, num, value ? true : false);
}

/**
 * @brief Read a pin's input level.
 * @return 1 if the input is high, 0 otherwise.
 * @req REQ-ROVARI-GPIO-0012
 */
uint8_t digital_read(pin_t pin)
{
    uint8_t port = ROVARI_PORT(pin);
    uint8_t num  = ROVARI_PIN_NUM(pin);
    SEVS_INVARIANT(port < NUM_PORTS);

    return gpio_get(port, num) ? 1 : 0;
}

/**
 * @brief Invert a pin's output level.
 * @req REQ-ROVARI-GPIO-0013
 */
void pin_toggle(pin_t pin)
{
    uint8_t port = ROVARI_PORT(pin);
    uint8_t num  = ROVARI_PIN_NUM(pin);
    SEVS_INVARIANT(port < NUM_PORTS);

    gpio_toggle(port, num);
}