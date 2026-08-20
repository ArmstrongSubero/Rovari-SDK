/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_defs.h - Pin definitions, constants, and mode enumerations
 *                 CH32V003 variant (RV32EC, 16K/2K)
 *
 * Available GPIO:
 *   Port A: PA1, PA2 only (PA0 is NRST on most packages)
 *   Port C: PC0 - PC7
 *   Port D: PD0 - PD7
 *   No Port B, no Port E.
 *
 * Pin encoding: port (bits 7:4) and pin number (bits 3:0).
 *   Port indices: 0=A, 2=C, 3=D (1 skipped, no GPIOB)
 */

#ifndef ROVARI_DEFS_H
#define ROVARI_DEFS_H

#include <stdint.h>

/* Pin encoding */
typedef uint8_t pin_t;

#define ROVARI_PIN(port, pin)  ((uint8_t)(((port) << 4) | ((pin) & 0x0F)))
#define ROVARI_PORT(p)         (((p) >> 4) & 0x0F)
#define ROVARI_PIN_NUM(p)      ((p) & 0x0F)
#define ROVARI_PIN_MASK(p)     ((uint16_t)(1U << ROVARI_PIN_NUM(p)))

/* Port A (only PA1, PA2 usable on most packages) */
#define PA1   ROVARI_PIN(0, 1)
#define PA2   ROVARI_PIN(0, 2)

/* Port C */
#define PC0   ROVARI_PIN(2, 0)
#define PC1   ROVARI_PIN(2, 1)
#define PC2   ROVARI_PIN(2, 2)
#define PC3   ROVARI_PIN(2, 3)
#define PC4   ROVARI_PIN(2, 4)
#define PC5   ROVARI_PIN(2, 5)
#define PC6   ROVARI_PIN(2, 6)
#define PC7   ROVARI_PIN(2, 7)

/* Port D */
#define PD0   ROVARI_PIN(3, 0)
#define PD1   ROVARI_PIN(3, 1)
#define PD2   ROVARI_PIN(3, 2)
#define PD3   ROVARI_PIN(3, 3)
#define PD4   ROVARI_PIN(3, 4)
#define PD5   ROVARI_PIN(3, 5)
#define PD6   ROVARI_PIN(3, 6)
#define PD7   ROVARI_PIN(3, 7)

/* Pin modes */
typedef enum {
    Output       = 0,   /* Push-pull output, 30 MHz */
    Input        = 1,   /* Floating input */
    InputPullUp  = 2,   /* Input with internal pull-up */
    InputPullDown= 3,   /* Input with internal pull-down */
    OutputOD     = 4,   /* Open-drain output */
    AF_PushPull  = 5,   /* Alternate function push-pull */
    AF_OpenDrain = 6,   /* Alternate function open-drain */
    Analog       = 7,   /* Analog input (for ADC) */
} PinMode;

/* Logic levels */
#define High  1
#define Low   0

/* UART instance identifiers
 * CH32V003 has USART1 only. */
typedef enum {
    SERIAL1 = 1,
} UartInstance;

/* I2C instance identifiers
 * CH32V003 has I2C1 only.
 * Default: SCL=PC2, SDA=PC1 */
typedef enum {
    I2C_1     = 1,   /* SCL=PC2, SDA=PC1 (default) */
} I2cInstance;

/* SPI instance identifiers
 * CH32V003 has SPI1 only. */
typedef enum {
    SPI_1 = 1,
} SpiInstance;

/* SPI configuration */
typedef struct {
    uint32_t speed;      /* Clock speed in Hz */
    uint8_t  mode;       /* SPI mode 0-3 */
    uint8_t  bit_order;  /* 0 = MSB first (default), 1 = LSB first */
} SpiConfig;

/* ADC constants - CH32V003 is 10-bit, VREF = VDD
 * Default assumes 5V operation. Override in project cflags
 * with -DADC_VREF_MV=3300 if running at 3.3V. */
#define ADC_MAX_VALUE   1023U
#ifndef ADC_VREF_MV
#define ADC_VREF_MV     5000U
#endif
#define ADC_RESOLUTION  10U

#endif /* ROVARI_DEFS_H */
