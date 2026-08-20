/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari
 *
 * rovari.h - Master include for the Rovari SDK (CH32V203)
 *
 * This is the only header users need:
 *   #include "rovari.h"
 *
 * Provides:
 *   - C++ classes: Gpio, Uart, Spi, I2c, Adc, Timer, Pwm, Capture
 *   - C functions: pin_mode(), digital_write(), uart_init(), etc.
 *   - System init: handled automatically before app_init()
 *   - Pin definitions: PA0-PA15, PB0-PB15, PC0-PC15, PD0-PD15
 *   - Constants: Output, Input, InputPullUp, InputPullDown, High, Low
 *
 * CH32V203 differences from CH32V307:
 *   - No Port E (GPIOE)
 *   - No DAC, RNG, DMA wrappers (hardware not present or deferred)
 *   - SERIAL1-4 only (not 5-8)
 *   - SPI1-2 only (not SPI3)
 *   - TIM1-4 user timers (SysTick used for millis/micros, TIM5 has no IRQ on D6)
 *   - USB CDC always running for bootloader re-entry
 */

#ifndef ROVARI_H
#define ROVARI_H

/* Pull in vendor HAL (wrapped in extern "C" for C++ safety) */
#ifdef __cplusplus
extern "C" {
#endif

#include "debug.h"

#ifdef __cplusplus
}
#endif

/* Rovari sub-headers */
#include "rovari_defs.h"
#include "rovari_gpio.h"
#include "rovari_uart.h"
#include "rovari_spi.h"
#include "rovari_i2c.h"
#include "rovari_adc.h"
#include "rovari_exti.h"
#include "rovari_wdg.h"
#include "rovari_timer.h"
#include "rovari_pwm.h"
#include "rovari_capture.h"
#include "rovari_rtc.h"
#include "rovari_crc.h"
#include "rovari_flash.h"

/* Drivers */
#include "rovari_neopixel.h"

/* System delay (clean wrappers) */
#ifdef __cplusplus
extern "C" {
#endif

static inline void delay(uint32_t ms)    { Delay_Ms(ms); }
static inline void delay_ms(uint32_t ms) { Delay_Ms(ms); }
static inline void delay_us(uint32_t us) { Delay_Us(us); }

/**
 * Returns milliseconds since boot.
 * Uses TIM5 running at 1 kHz (CH32V203 has no TIM7).
 * Wraps after ~49 days.
 */
uint32_t millis(void);

/**
 * Returns microseconds since boot.
 * Uses TIM5 counter for sub-millisecond resolution.
 * Wraps after ~71 minutes.
 */
uint32_t micros(void);

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