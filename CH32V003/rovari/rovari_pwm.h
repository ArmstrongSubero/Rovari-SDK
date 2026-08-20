/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 * rovari_pwm.h - PWM output for CH32V003
 *
 * TIM1 default mapping (no remap):
 *   CH1=PD2, CH4=PC4
 * Complementary output (not SDK-managed):
 *   CH1N=PD0
 * Note: TIM2 is reserved for system tick.
 *       CH2/CH3 require AFIO remap (not default).
 */
#ifndef ROVARI_PWM_H
#define ROVARI_PWM_H
#include "rovari_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
void pwm_init(pin_t pin, uint32_t freq_hz);
void pwm_write(pin_t pin, uint8_t duty);
void pwm_write_pct(pin_t pin, uint8_t percent);
void pwm_write_us(pin_t pin, uint32_t pulse_us);
void pwm_write_raw(pin_t pin, uint16_t compare);
void pwm_stop(pin_t pin);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class Pwm {
public:
    Pwm(pin_t pin, uint32_t freq) : _pin(pin)
    {
        pwm_init(pin, freq);
    }

    void write(uint8_t duty)      { pwm_write(_pin, duty); }
    void writePct(uint8_t pct)    { pwm_write_pct(_pin, pct); }
    void writeUs(uint32_t us)     { pwm_write_us(_pin, us); }
    void writeRaw(uint16_t val)   { pwm_write_raw(_pin, val); }
    void stop()                   { pwm_stop(_pin); }

private:
    pin_t _pin;
};

#endif

#endif
