/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_gpio.h - GPIO abstraction (C functions + C++ Gpio class)
 *                 CH32V003: ports A (partial), C, D only.
 */

#ifndef ROVARI_GPIO_H
#define ROVARI_GPIO_H

#include "rovari_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

void pin_mode(pin_t pin, PinMode mode);
void digital_write(pin_t pin, uint8_t value);
uint8_t digital_read(pin_t pin);
void pin_toggle(pin_t pin);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Gpio {
public:
    Gpio(pin_t pin, PinMode mode) : _pin(pin) {
        pin_mode(pin, mode);
    }

    void write(uint8_t value) { digital_write(_pin, value); }
    uint8_t read()            { return digital_read(_pin); }
    void toggle()             { pin_toggle(_pin); }
    void high()               { write(High); }
    void low()                { write(Low); }

private:
    pin_t _pin;
};

#endif

#endif /* ROVARI_GPIO_H */
