/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 *
 * rovari_pwm.h - PWM output for Baochip-1x
 *
 * PWM pin mapping (from Dabao hardware):
 *   Slice 1: PB0(ch0), PB1(ch1), PB2(ch2), PB3(ch3)
 *   Slice 2: PC0(ch0), PC1(ch1), PC2(ch2), PC3(ch3)
 *
 * The Dabao SDK defines pwm_init(slice, freq) and pwm_stop(slice).
 * To avoid linker collisions, the Rovari pin-based wrappers use
 * prefixed symbols with macros mapping the user-facing names.
 */

#ifndef ROVARI_PWM_H
#define ROVARI_PWM_H

#include "rovari_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

void _rovari_pwm_init(pin_t pin, uint32_t freq_hz);
void _rovari_pwm_write(pin_t pin, uint8_t duty);
void _rovari_pwm_write_pct(pin_t pin, uint8_t percent);
void _rovari_pwm_write_raw(pin_t pin, uint16_t compare);
void _rovari_pwm_stop(pin_t pin);

/* User-facing names (must come after bao.h in the include chain) */
#define pwm_init(pin, freq)       _rovari_pwm_init((pin), (freq))
#define pwm_write(pin, duty)      _rovari_pwm_write((pin), (duty))
#define pwm_write_pct(pin, pct)   _rovari_pwm_write_pct((pin), (pct))
#define pwm_write_raw(pin, val)   _rovari_pwm_write_raw((pin), (val))
#define pwm_stop(pin)             _rovari_pwm_stop((pin))

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Pwm {
public:
    Pwm(pin_t pin, uint32_t freq) : _pin(pin)
    {
        _rovari_pwm_init(pin, freq);
    }

    void write(uint8_t duty)      { _rovari_pwm_write(_pin, duty); }
    void writePct(uint8_t pct)    { _rovari_pwm_write_pct(_pin, pct); }
    void writeRaw(uint16_t val)   { _rovari_pwm_write_raw(_pin, val); }
    void stop()                   { _rovari_pwm_stop(_pin); }

private:
    pin_t _pin;
};

#endif

#endif /* ROVARI_PWM_H */
