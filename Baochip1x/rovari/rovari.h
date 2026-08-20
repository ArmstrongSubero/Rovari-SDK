/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 *
 * rovari.h - Master include for the Rovari SDK (Baochip-1x)
 *
 * This is the only header users need:
 *   #include "rovari.h"
 *
 * Baochip-1x peripheral set:
 *   GPIO (6 ports, 16 pins each), UART (4x UDMA), SPI (4x UDMA),
 *   I2C (4x UDMA), ADC (UDMA), PWM (hardware), BIO (programmable I/O),
 *   QSPI, RRAM, RTC, WDT, SHA-256, AES-128/256, TRNG, W25Q flash
 */

#ifndef ROVARI_H
#define ROVARI_H

/* Pull in Dabao SDK vendor headers.
 * bao.h provides: millis(), micros(), delay_ms(), delay_us(),
 * mini_printf(), and all hardware driver includes.
 * MUST come before Rovari headers so that SDK function declarations
 * are visible before any macro redirections. */
#ifdef __cplusplus
extern "C" {
#endif

#include "bao.h"

#ifdef __cplusplus
}
#endif

/* Rovari sub-headers */
#include "rovari_defs.h"
#include "rovari_gpio.h"
#include "rovari_button.h"
#include "rovari_pwm.h"
#include "rovari_tone.h"
#include "rovari_adc.h"
#include "rovari_spi.h"
#include "rovari_i2c.h"
#include "rovari_uart.h"

/* System delay wrapper */
#ifdef __cplusplus
extern "C" {
#endif

static inline void delay(uint32_t ms) { delay_ms(ms); }

#ifdef __cplusplus
}
#endif

/* App entry points (user implements these in .rova file) */
#ifdef __cplusplus
extern "C" {
#endif

void app_init(void);
void app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_H */