/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_lptim.h - Low-Power Timer (CH32H417)
 *
 * Two low-power timers (LPTIM1, LPTIM2) that run from LSI (40 kHz).
 * They continue running in Sleep and Stop modes for periodic wake-up.
 *
 * Usage:
 *   void on_tick() { led_toggle(); }
 *   lptim_start(1, 1000, on_tick);   // LPTIM1 fires every ~1 second
 *   lptim_stop(1);
 */

#ifndef ROVARI_LPTIM_H
#define ROVARI_LPTIM_H

#include "rovari_defs.h"

typedef void (*LptimCallback)(void);

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start a low-power timer with a periodic interrupt.
 *
 * Uses LSI (40 kHz) with /128 prescaler = ~312.5 Hz tick.
 * Resolution is ~3.2 ms. Max period is ~210 seconds.
 *
 * The timer continues running in Sleep and Stop modes.
 *
 * @param instance   1 (LPTIM1) or 2 (LPTIM2)
 * @param period_ms  Desired period in milliseconds (3-210000)
 * @param callback   Function called on each period match
 */
void lptim_start(uint8_t instance, uint32_t period_ms, LptimCallback callback);

/**
 * Stop a low-power timer.
 */
void lptim_stop(uint8_t instance);

/**
 * Read the current counter value.
 */
uint16_t lptim_get_counter(uint8_t instance);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_LPTIM_H */
