/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_pwm.h - PWM output on timer channels (CH32H417)
 *
 * PWM pin mapping (3.3V VDDIO domain pins):
 *   TIM4: CH1=PD12, CH2=PD13, CH3=PD14, CH4=PD15  (HB1, 150 MHz, AF2)
 *   TIM3: CH3=PD5                                   (HB1, 150 MHz, AF9)
 *
 * All timers run at HCLK (150 MHz with default PLL).
 *
 * Usage:
 *   pwm_init(PD12, 1000);       // 1 kHz PWM on PD12 (TIM4 CH1)
 *   pwm_write(PD12, 128);       // 50% duty (0-255 scale)
 *   pwm_write_us(PD12, 1500);   // 1500 us pulse (for servos)
 */

#ifndef ROVARI_PWM_H
#define ROVARI_PWM_H

#include "rovari_defs.h"

/* =========================================================================
 *  C API
 * ========================================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize PWM output on a timer-capable pin.
 *
 * The SDK looks up which timer and channel the pin belongs to,
 * enables the clock, configures the AF mux and pin as alternate-function
 * push-pull, and starts PWM at the requested frequency with 0% duty.
 *
 *   pwm_init(PD12, 1000);    // 1 kHz on PD12 (TIM4 CH1)
 *   pwm_init(PD14, 25000);   // 25 kHz on PD14 (TIM4 CH3) - fan control
 *   pwm_init(PD13, 50);      // 50 Hz on PD13 (TIM4 CH2) - servo
 *
 * @param pin       Timer-capable pin (PD12-PD15, PD5)
 * @param freq_hz   PWM frequency in Hz (1-1000000)
 */
void pwm_init(pin_t pin, uint32_t freq_hz);

/**
 * Set PWM duty cycle using an 8-bit value (0-255).
 * 0 = fully off, 255 = fully on.
 */
void pwm_write(pin_t pin, uint8_t duty);

/**
 * Set PWM duty cycle as a percentage (0.0-100.0).
 */
void pwm_write_pct(pin_t pin, float percent);

/**
 * Set PWM pulse width in microseconds.
 * Useful for servo control (typically 500-2500 us at 50 Hz).
 */
void pwm_write_us(pin_t pin, uint32_t pulse_us);

/**
 * Set PWM duty cycle using the raw timer compare value (0-period).
 */
void pwm_write_raw(pin_t pin, uint16_t compare);

/**
 * Stop PWM on a pin and return it to GPIO output low.
 */
void pwm_stop(pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_PWM_H */
