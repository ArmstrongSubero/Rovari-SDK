/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_button.c
 * @brief Debounced button input for Baochip-1x.
 *
 * Each button uses 16 bytes of RAM. Up to BUTTON_MAX (4) buttons.
 * The state machine runs inside pressed()/released()/held()/state()
 * calls, guarded by millis() so it executes at most once per ms
 * regardless of how many query calls happen per loop iteration.
 */

#include <stddef.h>
#include <stdint.h>
/* no vendor header needed */
#include "rovari_button.h"
#include "rovari_gpio.h"

/* Forward declaration for millis() */
extern uint64_t millis(void);

typedef struct {
    pin_t    pin;
    uint8_t  active;        /* 1 if this slot is registered */
    uint8_t  raw_last;      /* last raw reading */
    uint8_t  debounced;     /* current debounced state */
    uint8_t  fell;          /* edge flag: High to Low transition */
    uint8_t  rose;          /* edge flag: Low to High transition */
    uint32_t change_ms;     /* millis() when raw last changed */
    uint32_t held_since;    /* millis() when debounced went Low */
    uint32_t update_ms;     /* millis() of last state machine run */
} button_state_t;

static button_state_t s_buttons[BUTTON_MAX];

/**
 * @brief Find the state slot for a pin, or NULL.
 */
static button_state_t* find(pin_t pin)
{
    for (uint8_t i = 0; i < BUTTON_MAX; i++)
    {
        if (s_buttons[i].active && s_buttons[i].pin == pin)
            return &s_buttons[i];
    }
    return NULL;
}

/**
 * @brief Run the debounce state machine. Idempotent per ms tick.
 */
static void update(button_state_t* b)
{
    uint32_t now = millis();
    if (now == b->update_ms && b->update_ms != 0)
        return;  /* already ran this millisecond */
    b->update_ms = now;

    uint8_t raw = digital_read(b->pin);

    /* Raw state changed: restart debounce timer */
    if (raw != b->raw_last)
    {
        b->raw_last = raw;
        b->change_ms = now;
    }

    /* Stable for long enough: accept the new state */
    if ((now - b->change_ms) >= BUTTON_DEBOUNCE_MS)
    {
        if (b->raw_last != b->debounced)
        {
            uint8_t prev = b->debounced;
            b->debounced = b->raw_last;

            if (prev == High && b->debounced == Low)
            {
                b->fell = 1;
                b->held_since = now;
            }
            if (prev == Low && b->debounced == High)
            {
                b->rose = 1;
            }
        }
    }
}

/* -----------------------------------------------------------------------
 *  Public C API
 * ----------------------------------------------------------------------- */

void button_begin(pin_t pin)
{
    /* Check if already registered */
    if (find(pin) != NULL)
        return;

    /* Find empty slot */
    for (uint8_t i = 0; i < BUTTON_MAX; i++)
    {
        if (!s_buttons[i].active)
        {
            s_buttons[i].pin        = pin;
            s_buttons[i].active     = 1;
            s_buttons[i].raw_last   = High;
            s_buttons[i].debounced  = High;
            s_buttons[i].fell       = 0;
            s_buttons[i].rose       = 0;
            s_buttons[i].change_ms  = 0;
            s_buttons[i].held_since = 0;
            s_buttons[i].update_ms  = 0;

            pin_mode(pin, InputPullUp);
            return;
        }
    }
    /* No slot available, silently ignored */
}

uint8_t button_pressed(pin_t pin)
{
    button_state_t* b = find(pin);
    if (b == NULL) return 0;
    update(b);
    if (b->fell)
    {
        b->fell = 0;  /* consume the edge */
        return 1;
    }
    return 0;
}

uint8_t button_released(pin_t pin)
{
    button_state_t* b = find(pin);
    if (b == NULL) return 0;
    update(b);
    if (b->rose)
    {
        b->rose = 0;  /* consume the edge */
        return 1;
    }
    return 0;
}

uint8_t button_held(pin_t pin, uint32_t ms)
{
    button_state_t* b = find(pin);
    if (b == NULL) return 0;
    update(b);
    if (b->debounced == Low)
    {
        uint32_t elapsed = millis() - b->held_since;
        if (elapsed >= ms)
            return 1;
    }
    return 0;
}

uint8_t button_state(pin_t pin)
{
    button_state_t* b = find(pin);
    if (b == NULL) return High;
    update(b);
    return b->debounced;
}
