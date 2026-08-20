/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 */

/**
 * @file rovari_adc.c
 * @brief ADC wrapper for Baochip-1x.
 *
 * Maps Rovari pin_t to Dabao SDK ADC channels.
 * Only PC9 (channel 0) is available on the Dabao board.
 *
 * NOTE: This file does NOT include rovari_adc.h to avoid
 * the adc_init macro capturing calls to the Dabao SDK.
 */

#include <stdint.h>
#include "hardware/adc.h"
#include "rovari_defs.h"

static int pin_to_channel(pin_t pin)
{
    uint8_t port = ROVARI_PORT(pin);
    uint8_t num  = ROVARI_PIN_NUM(pin);

    /* PC9 = channel 0 (only accessible channel on Dabao) */
    if (port == 2 && num == 9) return 0;

    /* PA5 = channel 1, PA6 = channel 2, PA7 = channel 3 */
    if (port == 0 && num >= 5 && num <= 7) return (int)(num - 4);

    return 0;
}

void _rovari_adc_init(pin_t pin)
{
    (void)pin;
    adc_init();
}

uint16_t _rovari_analog_read(pin_t pin)
{
    int ch = pin_to_channel(pin);
    return (uint16_t)adc_read_raw((uint32_t)ch);
}

uint16_t _rovari_analog_read_mv(pin_t pin)
{
    int ch = pin_to_channel(pin);
    uint32_t raw = adc_read_raw((uint32_t)ch);
    return (uint16_t)adc_raw_to_mv(raw);
}
