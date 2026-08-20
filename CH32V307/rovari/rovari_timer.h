/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_timer.h - Hardware timer interrupts plus millis()/micros().
 *
 * @sevs-callbacks  Declares TimerCallback function-pointer dispatch API;
 *                  JPL Rule 9 suppressed per SEVS Section 2.10.
 *
 * Provides:
 *   millis() / micros()     system uptime using the RISC-V mcycle counter
 *   timer_start()           periodic interrupt at a given frequency
 *   timer_start_ms()        periodic interrupt at a given period in ms
 *   timer_stop()            stop a running timer
 *
 * Available timers for periodic interrupts:
 *   TIMER2-TIMER7   (APB1, 72 MHz input)
 *   TIMER1, TIMER8  (APB2, 144 MHz input, advanced timers)
 *
 * Timer clock derivation:
 *   timer_clk = APB_clk (no doubling, since APB prescaler = 1 in Rovari config)
 *   interrupt_freq = timer_clk / ((prescaler+1) x (period+1))
 *
 * Typical professional rates:
 *   Motor control / PWM update:   10-40 kHz
 *   Audio sampling:               8-48 kHz
 *   PID control loops:            1-10 kHz
 *   Sensor sampling (IMU, ADC):   100-1000 Hz
 *   Protocol timing:              1-10 kHz
 *   Debounce / button scan:       100-200 Hz
 *   Display refresh:              30-60 Hz
 *   Heartbeat LED:                1-2 Hz
 */

#ifndef ROVARI_TIMER_H
#define ROVARI_TIMER_H

#include "rovari_defs.h"

/* Timer instance identifiers */
typedef enum {
    TIMER1  = 1,
    TIMER2  = 2,
    TIMER3  = 3,
    TIMER4  = 4,
    TIMER5  = 5,
    TIMER6  = 6,
    TIMER7  = 7,    /* Reserved for millis()/micros(); do not use */
    TIMER8  = 8,
} TimerInstance;

/* Callback type */
typedef void (*TimerCallback)(void);

/* -----------------------------------------------------------------------
 *  C API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start a hardware timer that fires a periodic interrupt.
 *
 * The SDK automatically selects the prescaler and period to achieve
 * the requested frequency as closely as possible.
 *
 *   timer_start(TIMER2, 1000, pid_update);  // 1 kHz PID loop
 *   timer_start(TIMER3, 20000, adc_read);   // 20 kHz ADC sampling
 *   timer_start(TIMER4, 100, sensor_poll);  // 100 Hz sensor read
 *   timer_start(TIMER5, 2, heartbeat);      // 2 Hz heartbeat LED
 *
 * @param inst      TIMER1-TIMER8
 * @param freq_hz   Desired interrupt frequency in Hz (1-1000000)
 * @param callback  Function called on each timer interrupt
 */
void timer_start(TimerInstance inst, uint32_t freq_hz, TimerCallback callback);

/**
 * Start a hardware timer using a period in milliseconds.
 * Convenience wrapper for slow-rate tasks.
 *
 *   timer_start_ms(TIMER2, 500, blink);     // Every 500 ms
 *   timer_start_ms(TIMER3, 5000, log);      // Every 5 seconds
 *   timer_start_ms(TIMER4, 30000, report);  // Every 30 seconds
 *
 * Maximum period: ~60,000 ms (60 seconds) on APB1 timers.
 *
 * @param inst        TIMER1-TIMER8
 * @param period_ms   Desired period in milliseconds (1-60000)
 * @param callback    Function called on each timer interrupt
 */
void timer_start_ms(TimerInstance inst, uint32_t period_ms, TimerCallback callback);

/**
 * Stop a running timer and disable its interrupt.
 */
void timer_stop(TimerInstance inst);

/**
 * Set the frequency of an already-running timer without stopping it.
 */
void timer_set_freq(TimerInstance inst, uint32_t freq_hz);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_TIMER_H */
