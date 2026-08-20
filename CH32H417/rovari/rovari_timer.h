/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_timer.h - Hardware timer interrupts (CH32H417)
 *
 * Available timers for periodic interrupts:
 *   TIMER2-TIMER6    (HB1, 150 MHz input)
 *   TIMER1, TIMER8   (HB2, 150 MHz input, advanced timers)
 *   TIMER9-TIMER12   (HB2, 150 MHz input)
 *
 * TIMER7 is reserved for millis()/micros() system tick.
 *
 * All timers run at HCLK (150 MHz with default PLL).
 * No APB prescaler doubling (unlike STM32/CH32V307).
 *   interrupt_freq = HCLK / ((prescaler+1) x (period+1))
 */

#ifndef ROVARI_TIMER_H
#define ROVARI_TIMER_H

#include "rovari_defs.h"

/* -- Timer instance identifiers ------------------------------------------- */
typedef enum {
    TIMER1  = 1,
    TIMER2  = 2,
    TIMER3  = 3,
    TIMER4  = 4,
    TIMER5  = 5,
    TIMER6  = 6,
    TIMER7  = 7,    /* Reserved for millis()/micros() - do not use */
    TIMER8  = 8,
    TIMER9  = 9,
    TIMER10 = 10,
    TIMER11 = 11,
    TIMER12 = 12,
} TimerInstance;

/* -- Callback type -------------------------------------------------------- */
typedef void (*TimerCallback)(void);

/* =========================================================================
 *  C API
 * ========================================================================= */
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
 * @param inst      TIMER1-TIMER12 (except TIMER7)
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
 * Maximum period: ~65 seconds on all timers.
 *
 * @param inst        TIMER1-TIMER12 (except TIMER7)
 * @param period_ms   Desired period in milliseconds (1-65000)
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
