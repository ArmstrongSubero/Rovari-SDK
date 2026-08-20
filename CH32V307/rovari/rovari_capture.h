/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_capture.h - Timer input capture for pulse measurement.
 *
 * @sevs-callbacks  Declares CaptureCallback function-pointer API; JPL Rule 9
 *                  suppressed per SEVS Section 2.10.
 *
 * Measures:
 *   Pulse width (high or low time)
 *   Signal period (time between consecutive edges)
 *   Frequency (derived from the period)
 *
 * Uses the same timer channels as PWM (same pin mapping):
 *   TIM2: CH1=PA0,  CH2=PA1,  CH3=PA2,  CH4=PA3
 *   TIM3: CH1=PA6,  CH2=PA7,  CH3=PB0,  CH4=PB1
 *   TIM4: CH1=PB6,  CH2=PB7,  CH3=PB8,  CH4=PB9
 *
 * A pin cannot do PWM and input capture at the same time.
 *
 * Usage:
 *   capture_init(PA0, CaptureRising);
 *   uint32_t period_us = capture_read_period_us(PA0);
 *   uint32_t freq      = capture_read_freq(PA0);
 */

#ifndef ROVARI_CAPTURE_H
#define ROVARI_CAPTURE_H

#include "rovari_defs.h"

/* Edge selection */
typedef enum {
    CaptureRising  = 0,
    CaptureFalling = 1,
} CaptureEdge;

/* Callback type (optional, called on each capture event) */
typedef void (*CaptureCallback)(uint32_t capture_value);

/* -----------------------------------------------------------------------
 *  C API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize input capture on a timer-capable pin.
 *
 * Configures the timer to capture on the selected edge.
 * The timer runs at 1 MHz (1 us resolution) for easy measurement.
 *
 *   capture_init(PA0, CaptureRising);   // Capture rising edges on PA0
 *
 * @param pin    Timer-capable pin (same mapping as PWM)
 * @param edge   CaptureRising or CaptureFalling
 */
void capture_init(pin_t pin, CaptureEdge edge);

/**
 * Initialize input capture with a callback on each event.
 *
 *   void on_edge(uint32_t val) { ... }
 *   capture_init_cb(PA0, CaptureRising, on_edge);
 */
void capture_init_cb(pin_t pin, CaptureEdge edge, CaptureCallback callback);

/**
 * Read the last captured period in microseconds.
 * Period = time between two consecutive same-edge events.
 * Returns 0 if no capture has occurred yet.
 */
uint32_t capture_read_period_us(pin_t pin);

/**
 * Read the last captured pulse width in microseconds.
 * Uses PWM input mode (two channels on one pin) to measure
 * the high-time of the signal.
 * Returns 0 if no capture has occurred yet.
 */
uint32_t capture_read_pulse_us(pin_t pin);

/**
 * Read the signal frequency in Hz, derived from period.
 * Returns 0 if no capture has occurred yet.
 */
uint32_t capture_read_freq(pin_t pin);

/**
 * Stop input capture on a pin.
 */
void capture_stop(pin_t pin);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_CAPTURE_H */
