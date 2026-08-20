/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 */

/**
 * @file rovari_pwm.c
 * @brief PWM output for Baochip-1x.
 *
 * Maps Rovari pin_t identifiers to Dabao SDK (slice, channel) pairs.
 *   Slice 1: PB0(ch0), PB1(ch1), PB2(ch2), PB3(ch3)
 *   Slice 2: PC0(ch0), PC1(ch1), PC2(ch2), PC3(ch3)
 *
 * NOTE: This file deliberately does NOT include rovari_pwm.h to avoid
 * the pwm_init macro capturing calls to the Dabao SDK's pwm_init().
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "rovari_defs.h"

#define PWM_DUTY_MAX  255U
#define PWM_PCT_MAX   100U

typedef struct {
    pin_t   pin;
    uint8_t port;
    uint8_t num;
    uint8_t slice;
    uint8_t channel;
} pwm_pin_def_t;

static const pwm_pin_def_t pwm_pins[] = {
    { PB0, 1, 0,  1, 0 },
    { PB1, 1, 1,  1, 1 },
    { PB2, 1, 2,  1, 2 },
    { PB3, 1, 3,  1, 3 },
    { PC0, 2, 0,  2, 0 },
    { PC1, 2, 1,  2, 1 },
    { PC2, 2, 2,  2, 2 },
    { PC3, 2, 3,  2, 3 },
};

#define PWM_PIN_COUNT (sizeof(pwm_pins) / sizeof(pwm_pins[0]))

/* Cached period per slice (set during init) */
static uint16_t s_period[4] = {0, 0, 0, 0};

static const pwm_pin_def_t* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < PWM_PIN_COUNT; i++) {
        if (pwm_pins[i].pin == pin) return &pwm_pins[i];
    }
    return (void*)0;
}

/* -----------------------------------------------------------------------
 *  Public C API (prefixed names, mapped by macros in rovari_pwm.h)
 * ----------------------------------------------------------------------- */

void _rovari_pwm_init(pin_t pin, uint32_t freq_hz)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == (void*)0) return;

    /* Set GPIO to PWM alternate function */
    pwm_init_pin(def->port, def->num);

    /* Initialize the slice (Dabao SDK function, no name collision here) */
    uint16_t period = pwm_init(def->slice, freq_hz);
    s_period[def->slice] = period;

    /* Start at 0% duty */
    pwm_set_duty(def->slice, def->channel, 0);
}

void _rovari_pwm_write(pin_t pin, uint8_t duty)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == (void*)0) return;

    uint16_t period = s_period[def->slice];
    uint16_t compare = (uint16_t)(((uint32_t)duty * period) / PWM_DUTY_MAX);
    pwm_set_duty(def->slice, def->channel, compare);
}

void _rovari_pwm_write_pct(pin_t pin, uint8_t percent)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == (void*)0) return;

    if (percent > PWM_PCT_MAX) percent = PWM_PCT_MAX;
    uint16_t period = s_period[def->slice];
    uint16_t compare = (uint16_t)(((uint32_t)percent * period) / PWM_PCT_MAX);
    pwm_set_duty(def->slice, def->channel, compare);
}

void _rovari_pwm_write_raw(pin_t pin, uint16_t compare)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == (void*)0) return;

    pwm_set_duty(def->slice, def->channel, compare);
}

void _rovari_pwm_stop(pin_t pin)
{
    const pwm_pin_def_t* def = find_pin(pin);
    if (def == (void*)0) return;

    pwm_set_duty(def->slice, def->channel, 0);
}
