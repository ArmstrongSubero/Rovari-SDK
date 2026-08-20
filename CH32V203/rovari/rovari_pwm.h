/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_pwm.h — PWM output on timer channels
 *
 * Default channel-to-pin mapping (no remap):
 *   TIM1: CH1=PA8,  CH2=PA9,  CH3=PA10, CH4=PA11  (APB2, 144 MHz)
 *   TIM2: CH1=PA0,  CH2=PA1,  CH3=PA2,  CH4=PA3   (APB1, 72 MHz)
 *   TIM3: CH1=PA6,  CH2=PA7,  CH3=PB0,  CH4=PB1   (APB1, 72 MHz)
 *   TIM4: CH1=PB6,  CH2=PB7,  CH3=PB8,  CH4=PB9   (APB1, 72 MHz)
 *
 * Usage:
 *   pwm_init(PA0, 1000);       // 1 kHz PWM on PA0 (TIM2 CH1)
 *   pwm_write(PA0, 128);       // 50% duty (0–255 scale)
 *   pwm_write_us(PA0, 1500);   // 1500 us pulse (for servos)
 */

#ifndef ROVARI_PWM_H
#define ROVARI_PWM_H

#include "rovari_defs.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  C API
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize PWM output on a timer-capable pin.
 *
 * The SDK looks up which timer and channel the pin belongs to,
 * enables the clock, configures the pin as alternate-function push-pull,
 * and starts PWM at the requested frequency with 0% duty.
 *
 *   pwm_init(PA0, 1000);    // 1 kHz on PA0 (TIM2 CH1)
 *   pwm_init(PB0, 25000);   // 25 kHz on PB0 (TIM3 CH3) — fan control
 *   pwm_init(PA8, 50);      // 50 Hz on PA8 (TIM1 CH1) — servo
 *
 * @param pin       Timer-capable pin (e.g. PA0, PA6, PB0, PA8)
 * @param freq_hz   PWM frequency in Hz (1–1000000)
 */
void pwm_init(pin_t pin, uint32_t freq_hz);

/**
 * Set PWM duty cycle using an 8-bit value (0–255).
 * 0 = fully off, 255 = fully on.
 * Maps linearly to the timer period.
 *
 *   pwm_write(PA0, 0);      // 0% duty
 *   pwm_write(PA0, 128);    // ~50% duty
 *   pwm_write(PA0, 255);    // 100% duty
 */
void pwm_write(pin_t pin, uint8_t duty);

/**
 * Set PWM duty cycle as a percentage (0.0–100.0).
 *
 *   pwm_write_pct(PA0, 50.0f);   // 50% duty
 *   pwm_write_pct(PA0, 33.3f);   // 33.3% duty
 */
void pwm_write_pct(pin_t pin, float percent);

/**
 * Set PWM pulse width in microseconds.
 * Useful for servo control (typically 500–2500 us).
 *
 *   pwm_init(PA8, 50);           // 50 Hz servo frequency
 *   pwm_write_us(PA8, 1500);     // Center position
 *   pwm_write_us(PA8, 1000);     // Min position
 *   pwm_write_us(PA8, 2000);     // Max position
 */
void pwm_write_us(pin_t pin, uint32_t pulse_us);

/**
 * Set PWM duty cycle using the raw timer compare value (0–period).
 * For advanced users who need exact control.
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
