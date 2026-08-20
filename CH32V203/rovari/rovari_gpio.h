/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari
 *
 * rovari_gpio.h - GPIO abstraction (C functions + C++ Gpio class)
 */

#ifndef ROVARI_GPIO_H
#define ROVARI_GPIO_H

#include "rovari_defs.h"

/* ===================================================================
 *  C API - works in both .c and .rova files
 * =================================================================== */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure a pin's mode. Automatically enables the port clock.
 *   pin_mode(PA5, Output);
 */
void pin_mode(pin_t pin, PinMode mode);

/**
 * Set a pin high or low.
 *   digital_write(PA5, High);
 */
void digital_write(pin_t pin, uint8_t value);

/**
 * Read a pin's state. Returns High (1) or Low (0).
 *   uint8_t state = digital_read(PA5);
 */
uint8_t digital_read(pin_t pin);

/**
 * Toggle a pin's output state.
 *   pin_toggle(PA5);
 */
void pin_toggle(pin_t pin);

#ifdef __cplusplus
}
#endif

/* ===================================================================
 *  C++ API - available in .rova files (compiled as C++)
 * =================================================================== */
#ifdef __cplusplus

class Gpio {
public:
    /**
     * Construct a GPIO pin. Hardware init is deferred until first use
     * or until begin() is called, so global declarations are safe:
     *   Gpio led(PA5, Output);     // safe as global
     */
    Gpio(pin_t pin, PinMode mode) : _pin(pin), _mode(mode), _inited(false) {}

    /**
     * Explicitly initialize the hardware. Called automatically on
     * first write/read/toggle, but can be called manually:
     *   led.begin();
     */
    void begin() {
        if (!_inited) {
            pin_mode(_pin, _mode);
            _inited = true;
        }
    }

    /** Set pin high or low */
    void write(uint8_t value) {
        _ensure_init();
        digital_write(_pin, value);
    }

    /** Read pin state */
    uint8_t read() {
        _ensure_init();
        return digital_read(_pin);
    }

    /** Toggle pin output */
    void toggle() {
        _ensure_init();
        pin_toggle(_pin);
    }

    /** Convenience: set high */
    void high() { write(High); }

    /** Convenience: set low */
    void low()  { write(Low); }

    /** Assignment operator for natural syntax: led = High; */
    Gpio& operator=(uint8_t value) {
        write(value);
        return *this;
    }

    /** Bool conversion for natural reads: if (button) { ... } */
    operator bool() {
        return read() != 0;
    }

    /** Get the underlying pin identifier */
    pin_t pin() const { return _pin; }

private:
    pin_t   _pin;
    PinMode _mode;
    bool    _inited;

    void _ensure_init() {
        if (!_inited) {
            pin_mode(_pin, _mode);
            _inited = true;
        }
    }
};

#endif /* __cplusplus */

#endif /* ROVARI_GPIO_H */