/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_exti.h — External interrupt abstraction (C functions + C++ helpers)
 *
 * EXTI maps a GPIO pin to a hardware interrupt line.  There are 16 EXTI
 * lines (0–15), one per pin number.  Only one port can be connected to
 * each line at a time: PA0 or PB0 or PC0 can use EXTI0, but not two
 * of them simultaneously.
 *
 * Usage:
 *   void my_callback() { led.toggle(); }
 *   attach_interrupt(PA1, Falling, my_callback);
 */

#ifndef ROVARI_EXTI_H
#define ROVARI_EXTI_H

#include "rovari_defs.h"

/* ── Edge trigger modes ─────────────────────────────────────────────── */
typedef enum {
    Rising  = 0,   /* Trigger on low-to-high transition */
    Falling = 1,   /* Trigger on high-to-low transition */
    Change  = 2,   /* Trigger on both edges */
} EdgeMode;

/* ── Callback type ──────────────────────────────────────────────────── */
typedef void (*ExtiCallback)(void);

/* ═══════════════════════════════════════════════════════════════════════
 *  C API — works in both .c and .rova files
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Attach an interrupt to a GPIO pin.
 *
 * Configures the pin as input (if not already), connects the pin to
 * the corresponding EXTI line, enables the EXTI interrupt in the
 * NVIC/PFIC, and registers the callback.
 *
 * Only one callback can be registered per EXTI line (pin number).
 * Calling attach_interrupt again on the same pin number replaces
 * the previous callback.
 *
 * The pin's input mode should be configured before calling this
 * function (e.g., InputPullUp for a button).
 *
 *   pin_mode(PA1, InputPullUp);
 *   attach_interrupt(PA1, Falling, my_callback);
 *
 * @param pin       Pin identifier (PA0, PB3, etc.)
 * @param edge      Rising, Falling, or Change
 * @param callback  Function to call when the interrupt fires
 */
void attach_interrupt(pin_t pin, EdgeMode edge, ExtiCallback callback);

/**
 * Detach the interrupt from a pin.  Disables the EXTI line and
 * clears the registered callback.
 */
void detach_interrupt(pin_t pin);

/**
 * Temporarily disable all interrupts.  Returns the previous
 * interrupt state for use with interrupts_restore().
 *
 *   uint32_t state = interrupts_disable();
 *   // critical section — no interrupts fire here
 *   interrupts_restore(state);
 */
uint32_t interrupts_disable(void);

/**
 * Restore the interrupt state saved by interrupts_disable().
 */
void interrupts_restore(uint32_t state);

/**
 * Enable global interrupts.
 */
void interrupts_enable(void);

#ifdef __cplusplus
}
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  C++ convenience — available in .rova files
 * ═══════════════════════════════════════════════════════════════════════ */
#ifdef __cplusplus

/**
 * RAII critical section guard.  Disables interrupts on construction,
 * restores on destruction.
 *
 *   {
 *       CriticalSection lock;
 *       // interrupts disabled in this scope
 *       shared_counter++;
 *   }  // interrupts restored here
 */
class CriticalSection {
public:
    CriticalSection()  { _state = interrupts_disable(); }
    ~CriticalSection() { interrupts_restore(_state); }

    /* Non-copyable */
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
    uint32_t _state;
};

#endif /* __cplusplus */

#endif /* ROVARI_EXTI_H */
