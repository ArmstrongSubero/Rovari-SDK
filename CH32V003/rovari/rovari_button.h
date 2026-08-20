/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_button.h - Debounced button input with edge detection
 *
 * Non-blocking, millis() based. Supports up to 4 simultaneous
 * buttons (64 bytes total RAM on V003).
 *
 * C usage:
 *   button_begin(PC2);
 *   // in app_run:
 *   if (button_pressed(PC2))  { ... }  // true once per press
 *   if (button_released(PC2)) { ... }  // true once per release
 *   if (button_held(PC2, 1000)) { ... } // true while held 1s+
 *
 * C++ usage:
 *   Button btn(PC2);
 *   // in app_run:
 *   if (btn.pressed())   { ... }
 *   if (btn.released())  { ... }
 *   if (btn.held(1000))  { ... }
 */

#ifndef ROVARI_BUTTON_H
#define ROVARI_BUTTON_H

#include "rovari_defs.h"

#define BUTTON_MAX       4
#define BUTTON_DEBOUNCE_MS  20

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register a button pin. Configures as InputPullUp.
 * Call once in app_init().
 */
void button_begin(pin_t pin);

/**
 * Returns 1 once per debounced press (falling edge, active low).
 * Auto-updates the debounce state machine internally.
 */
uint8_t button_pressed(pin_t pin);

/**
 * Returns 1 once per debounced release (rising edge).
 */
uint8_t button_released(pin_t pin);

/**
 * Returns 1 while the button has been held down for at least ms milliseconds.
 */
uint8_t button_held(pin_t pin, uint32_t ms);

/**
 * Returns the current debounced state (High or Low).
 */
uint8_t button_state(pin_t pin);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Button {
public:
    Button(pin_t pin, PinMode mode = InputPullUp) : _pin(pin)
    {
        pin_mode(pin, mode);
        button_begin(pin);
    }

    bool pressed()              { return button_pressed(_pin) != 0; }
    bool released()             { return button_released(_pin) != 0; }
    bool held(uint32_t ms)      { return button_held(_pin, ms) != 0; }
    uint8_t state()             { return button_state(_pin); }

private:
    pin_t _pin;
};

#endif

#endif /* ROVARI_BUTTON_H */
