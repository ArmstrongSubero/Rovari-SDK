/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_wdg.h — Watchdog timer abstraction (IWDG + WWDG)
 *
 * IWDG (Independent Watchdog):
 *   Clocked from the 40 kHz LSI oscillator.  Simple countdown timer.
 *   If not fed before it reaches zero, the chip resets.
 *   Cannot be stopped once started (hardware limitation).
 *
 * WWDG (Window Watchdog):
 *   Clocked from APB1 (72 MHz / 4096 / prescaler).
 *   Must be fed within a time window: not too early, not too late.
 *   Can generate an early-warning interrupt before reset.
 */

#ifndef ROVARI_WDG_H
#define ROVARI_WDG_H

#include "rovari_defs.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  C API
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus
extern "C" {
#endif

/* ── Independent Watchdog (IWDG) ────────────────────────────────────── */

/**
 * Start the independent watchdog with a timeout in milliseconds.
 * Once started, it CANNOT be stopped.  You must call iwdg_feed()
 * before the timeout expires, or the chip resets.
 *
 * The LSI oscillator runs at ~40 kHz (uncalibrated, can vary 30–60 kHz).
 * Available timeouts depend on prescaler and reload value:
 *   Prescaler /4:    0.1 ms – 409.6 ms
 *   Prescaler /8:    0.2 ms – 819.2 ms
 *   Prescaler /16:   0.4 ms – 1638.4 ms
 *   Prescaler /32:   0.8 ms – 3276.8 ms
 *   Prescaler /64:   1.6 ms – 6553.6 ms
 *   Prescaler /128:  3.2 ms – 13107.2 ms
 *   Prescaler /256:  6.4 ms – 26214.4 ms (~26 seconds max)
 *
 *   iwdg_start(1000);   // 1-second watchdog
 *
 * @param timeout_ms  Desired timeout in milliseconds (max ~26000)
 */
void iwdg_start(uint32_t timeout_ms);

/**
 * Feed (reload) the watchdog.  Resets the countdown to the configured
 * timeout.  Must be called periodically from your main loop.
 */
void iwdg_feed(void);

/* ── Window Watchdog (WWDG) ─────────────────────────────────────────── */

/**
 * Start the window watchdog.
 *
 * The WWDG counts down from 'counter' (max 127).  A reset occurs if:
 *   - The counter reaches 0x40 (timeout — fed too late)
 *   - You feed it while the counter is above 'window' (fed too early)
 *
 * This "window" prevents both stuck-loop and runaway-loop failures.
 *
 *   wwdg_start(127, 80);  // Counter starts at 127, window at 80
 *                          // Must feed when counter is between 80 and 64
 *
 * @param counter   Initial/reload counter value (must be 0x40–0x7F)
 * @param window    Window threshold (must be 0x40–0x7F, and < counter)
 */
void wwdg_start(uint8_t counter, uint8_t window);

/**
 * Feed (reload) the window watchdog.
 * Must only be called when the counter is within the window
 * (between window threshold and 0x40), otherwise the chip resets.
 */
void wwdg_feed(uint8_t counter);

/**
 * Check if the last reset was caused by the IWDG.
 * Call this at the top of app_init() before clearing reset flags.
 * Returns 1 if IWDG caused the reset, 0 otherwise.
 */
uint8_t iwdg_was_reset(void);

/**
 * Check if the last reset was caused by the WWDG.
 * Returns 1 if WWDG caused the reset, 0 otherwise.
 */
uint8_t wwdg_was_reset(void);

/**
 * Clear the watchdog reset flags.
 * Call after checking iwdg_was_reset() / wwdg_was_reset().
 */
void wdg_clear_reset_flags(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_WDG_H */
