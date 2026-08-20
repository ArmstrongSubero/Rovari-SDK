/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari.h - Master include for the Rovari SDK
 *
 * This is the only header users need:
 *   #include "rovari.h"
 *
 * Provides:
 *   C++ classes: Gpio, Uart, Spi, Adc, Dac, DmaAdc, DmaDac (I2c and Timer are planned)
 *   C functions: pin_mode(), digital_write(), uart_init(), and so on
 *   System init: handled automatically before app_init()
 *   Pin definitions: PA0 through PA15, PB0 through PB15, and so on
 *   Constants: Output, Input, InputPullUp, InputPullDown, High, Low
 */

#ifndef ROVARI_H
#define ROVARI_H

/* Pull in vendor HAL (wrapped in extern "C" for C++ safety) */
#ifdef __cplusplus
extern "C" {
#endif

#include "debug.h"          /* WCH HAL master include + Delay_Ms/Us */

#ifdef __cplusplus
}
#endif

/* Rovari sub-headers */
#include "rovari_defs.h"
#include "rovari_gpio.h"
#include "rovari_uart.h"
#include "rovari_spi.h"
#include "rovari_exti.h"
#include "rovari_wdg.h"
#include "rovari_timer.h"
#include "rovari_pwm.h"
#include "rovari_capture.h"
#include "rovari_i2c.h"    
#include "rovari_adc.h"
#include "rovari_dac.h"
#include "rovari_dma.h"
#include "rovari_rtc.h"
#include "rovari_rng.h"
#include "rovari_crc.h"
#include "rovari_flash.h"

/* __ Libraries __________________________________________________________*/

// Display
#include "rovari_ssd1306.h"
#include "rovari_sdcard.h"

/* System delay (clean wrappers) */
#ifdef __cplusplus
extern "C" {
#endif

static inline void delay(uint32_t ms)   { Delay_Ms(ms); }
static inline void delay_ms(uint32_t ms){ Delay_Ms(ms); }
static inline void delay_us(uint32_t us){ Delay_Us(us); }

/**
 * Returns milliseconds since boot.
 * Uses a dedicated hardware timer (TIM7) running at 1 kHz.
 * Initialized automatically at system startup.
 * Wraps after ~49 days (32-bit ms).
 */
uint32_t millis(void);

/**
 * Returns microseconds since boot.
 * Uses SysTick->CNT which counts at SystemCoreClock/8 rate.
 * Wraps after ~4295 seconds (~71 minutes) at 32-bit.
 */
uint32_t micros(void);

#ifdef __cplusplus
}
#endif

/* App entry points (user implements these in .rova file) */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Called once after system init. Set up your peripherals here.
 * Equivalent to Arduino's setup().
 */
void app_init(void);

/**
 * Called repeatedly in an infinite loop. Put your main logic here.
 * Equivalent to Arduino's loop().
 */
void app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_H */
