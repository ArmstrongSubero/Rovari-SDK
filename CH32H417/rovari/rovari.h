/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari.h - Master include for the Rovari SDK (CH32H417)
 *
 * This is the only header users need:
 *   #include "rovari.h"
 */

#ifndef ROVARI_H
#define ROVARI_H

/* -- Pull in vendor HAL (wrapped in extern "C" for C++ safety) ------ */
#ifdef __cplusplus
extern "C" {
#endif

#include "debug.h"          /* WCH HAL master include + Delay_Ms/Us */

#ifdef __cplusplus
}
#endif

/* -- Rovari sub-headers --------------------------------------------- */
#include "rovari_defs.h"
#include "rovari_gpio.h"
#include "rovari_uart.h"
#include "rovari_spi.h"
#include "rovari_dma.h"
#include "rovari_swi2c.h"
#include "rovari_i2c.h"

/* -- Driver headers (safe to include even if library not enabled) --- */
#include "rovari_touch.h"
#include "rovari_ft6336u.h"
#include "rovari_st7796s.h"

/* -- System delay (clean wrappers) ---------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

static inline void delay(uint32_t ms)   { Delay_Ms(ms); }
static inline void delay_ms(uint32_t ms){ Delay_Ms(ms); }
static inline void delay_us(uint32_t us){ Delay_Us(us); }

/**
 * Returns milliseconds since boot.
 * Uses TIM7 running at 1 kHz.
 */
uint32_t millis(void);

/**
 * Returns microseconds since boot.
 */
uint32_t micros(void);

#ifdef __cplusplus
}
#endif

/* -- App entry points (user implements these in .rova file) ---------- */
#ifdef __cplusplus
extern "C" {
#endif

void app_init(void);
void app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_H */
