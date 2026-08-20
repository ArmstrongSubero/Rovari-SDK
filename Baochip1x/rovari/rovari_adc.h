/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 *
 * rovari_adc.h - ADC abstraction for Baochip-1x
 *
 * ADC pin: PC9 (channel 0, only external channel on Dabao board)
 * 10-bit, 1.208V internal bandgap reference.
 *
 * The Dabao SDK defines adc_init(void) in hardware/adc.h.
 * The Rovari pin-based wrapper uses a prefixed symbol with
 * a macro mapping the user-facing name.
 */

#ifndef ROVARI_ADC_H
#define ROVARI_ADC_H

#include "rovari_defs.h"

/* Pull in the Dabao SDK ADC header BEFORE defining the adc_init
 * macro. This ensures the SDK's 'void adc_init(void)' declaration
 * is processed without macro interference, and the include guard
 * prevents re-processing if the user includes it again. */
#include "hardware/adc.h"

#ifdef __cplusplus
extern "C" {
#endif

void _rovari_adc_init(pin_t pin);
uint16_t _rovari_analog_read(pin_t pin);
uint16_t _rovari_analog_read_mv(pin_t pin);

/*
 * adc_init() accepts 0 or 1 arguments:
 *   adc_init()     uses the default Dabao ADC pin PC9 (SDK compatible)
 *   adc_init(pin)  uses the specified pin (Rovari API)
 */
#define _ROVARI_ADC_FIRST(a, ...) (a)
#define adc_init(...)  _rovari_adc_init( \
    _ROVARI_ADC_FIRST(__VA_ARGS__ __VA_OPT__(,) PC9))
#define analog_read(pin)     _rovari_analog_read(pin)
#define analog_read_mv(pin)  _rovari_analog_read_mv(pin)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Adc {
public:
    Adc(pin_t pin) : _pin(pin) {
        _rovari_adc_init(pin);
    }

    uint16_t read()    { return _rovari_analog_read(_pin); }
    uint16_t readMv()  { return _rovari_analog_read_mv(_pin); }

private:
    pin_t _pin;
};

#endif

#endif /* ROVARI_ADC_H */