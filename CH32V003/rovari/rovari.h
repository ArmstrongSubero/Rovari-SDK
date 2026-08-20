/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari.h - Master include for the Rovari SDK (CH32V003)
 *
 * This is the only header users need:
 *   #include "rovari.h"
 *
 * CH32V003 peripheral set:
 *   GPIO, UART (USART1), SPI (SPI1), I2C (I2C1), ADC (10-bit),
 *   PWM (TIM1), Timer (TIM1), Capture (TIM1), EXTI, IWDG, WWDG
 *
 * Not available on CH32V003:
 *   DAC, RNG, CRC, RTC, DMA (driver), SDIO, Ethernet, etc.
 */

#ifndef ROVARI_H
#define ROVARI_H

/* Pull in vendor HAL */
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
#include "rovari_exti.h"
#include "rovari_wdg.h"
#include "rovari_timer.h"
#include "rovari_pwm.h"
#include "rovari_capture.h"
#include "rovari_i2c.h"
#include "rovari_adc.h"
#include "rovari_button.h"
#include "rovari_tone.h"
#include "rovari_comparator.h"
#include "rovari_servo.h"

/* System delay wrappers */
#ifdef __cplusplus
extern "C" {
#endif

static inline void delay(uint32_t ms)   { Delay_Ms(ms); }
static inline void delay_ms(uint32_t ms){ Delay_Ms(ms); }
static inline void delay_us(uint32_t us){ Delay_Us(us); }

/**
 * Returns milliseconds since boot (TIM2-based, 1 kHz).
 */
uint32_t millis(void);

/**
 * Returns microseconds since boot.
 * Wraps after ~71 minutes (32-bit).
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
