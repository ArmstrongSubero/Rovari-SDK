/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_defs.h - Pin definitions, constants, and mode enumerations
 *
 * Pin naming: PA0, PA1, ... PA15, PB0, ... PF14
 * Each pin encodes port (bits 7:4) and pin number (bits 3:0).
 */

#ifndef ROVARI_DEFS_H
#define ROVARI_DEFS_H

#include <stdint.h>

/* -- Pin encoding --------------------------------------------------- */
/*  port_index (0=A,1=B,2=C,3=D,4=E,5=F) in upper nibble, pin 0-15 in lower */

typedef uint8_t pin_t;

#define ROVARI_PIN(port, pin)  ((uint8_t)(((port) << 4) | ((pin) & 0x0F)))
#define ROVARI_PORT(p)         (((p) >> 4) & 0x0F)
#define ROVARI_PIN_NUM(p)      ((p) & 0x0F)
#define ROVARI_PIN_MASK(p)     ((uint16_t)(1U << ROVARI_PIN_NUM(p)))

/* -- Port A --------------------------------------------------------- */
#define PA0   ROVARI_PIN(0, 0)
#define PA1   ROVARI_PIN(0, 1)
#define PA2   ROVARI_PIN(0, 2)
#define PA3   ROVARI_PIN(0, 3)
#define PA4   ROVARI_PIN(0, 4)
#define PA5   ROVARI_PIN(0, 5)
#define PA6   ROVARI_PIN(0, 6)
#define PA7   ROVARI_PIN(0, 7)
#define PA8   ROVARI_PIN(0, 8)
#define PA9   ROVARI_PIN(0, 9)
#define PA10  ROVARI_PIN(0, 10)
#define PA11  ROVARI_PIN(0, 11)
#define PA12  ROVARI_PIN(0, 12)
#define PA13  ROVARI_PIN(0, 13)
#define PA14  ROVARI_PIN(0, 14)
#define PA15  ROVARI_PIN(0, 15)

/* -- Port B --------------------------------------------------------- */
#define PB0   ROVARI_PIN(1, 0)
#define PB1   ROVARI_PIN(1, 1)
#define PB2   ROVARI_PIN(1, 2)
#define PB3   ROVARI_PIN(1, 3)
#define PB4   ROVARI_PIN(1, 4)
#define PB5   ROVARI_PIN(1, 5)
#define PB6   ROVARI_PIN(1, 6)
#define PB7   ROVARI_PIN(1, 7)
#define PB8   ROVARI_PIN(1, 8)
#define PB9   ROVARI_PIN(1, 9)
#define PB10  ROVARI_PIN(1, 10)
#define PB11  ROVARI_PIN(1, 11)
#define PB12  ROVARI_PIN(1, 12)
#define PB13  ROVARI_PIN(1, 13)
#define PB14  ROVARI_PIN(1, 14)
#define PB15  ROVARI_PIN(1, 15)

/* -- Port C --------------------------------------------------------- */
#define PC0   ROVARI_PIN(2, 0)
#define PC1   ROVARI_PIN(2, 1)
#define PC2   ROVARI_PIN(2, 2)
#define PC3   ROVARI_PIN(2, 3)
#define PC4   ROVARI_PIN(2, 4)
#define PC5   ROVARI_PIN(2, 5)
#define PC6   ROVARI_PIN(2, 6)
#define PC7   ROVARI_PIN(2, 7)
#define PC8   ROVARI_PIN(2, 8)
#define PC9   ROVARI_PIN(2, 9)
#define PC10  ROVARI_PIN(2, 10)
#define PC11  ROVARI_PIN(2, 11)
#define PC12  ROVARI_PIN(2, 12)
#define PC13  ROVARI_PIN(2, 13)
#define PC14  ROVARI_PIN(2, 14)
#define PC15  ROVARI_PIN(2, 15)

/* -- Port D --------------------------------------------------------- */
#define PD0   ROVARI_PIN(3, 0)
#define PD1   ROVARI_PIN(3, 1)
#define PD2   ROVARI_PIN(3, 2)
#define PD3   ROVARI_PIN(3, 3)
#define PD4   ROVARI_PIN(3, 4)
#define PD5   ROVARI_PIN(3, 5)
#define PD6   ROVARI_PIN(3, 6)
#define PD7   ROVARI_PIN(3, 7)
#define PD8   ROVARI_PIN(3, 8)
#define PD9   ROVARI_PIN(3, 9)
#define PD10  ROVARI_PIN(3, 10)
#define PD11  ROVARI_PIN(3, 11)
#define PD12  ROVARI_PIN(3, 12)
#define PD13  ROVARI_PIN(3, 13)
#define PD14  ROVARI_PIN(3, 14)
#define PD15  ROVARI_PIN(3, 15)

/* -- Port E --------------------------------------------------------- */
#define PE0   ROVARI_PIN(4, 0)
#define PE1   ROVARI_PIN(4, 1)
#define PE2   ROVARI_PIN(4, 2)
#define PE3   ROVARI_PIN(4, 3)
#define PE4   ROVARI_PIN(4, 4)
#define PE5   ROVARI_PIN(4, 5)
#define PE6   ROVARI_PIN(4, 6)
#define PE7   ROVARI_PIN(4, 7)
#define PE8   ROVARI_PIN(4, 8)
#define PE9   ROVARI_PIN(4, 9)
#define PE10  ROVARI_PIN(4, 10)
#define PE11  ROVARI_PIN(4, 11)
#define PE12  ROVARI_PIN(4, 12)
#define PE13  ROVARI_PIN(4, 13)
#define PE14  ROVARI_PIN(4, 14)
#define PE15  ROVARI_PIN(4, 15)

/* -- Port F (CH32H417 only) ----------------------------------------- */
#define PF0   ROVARI_PIN(5, 0)
#define PF1   ROVARI_PIN(5, 1)
#define PF2   ROVARI_PIN(5, 2)
#define PF3   ROVARI_PIN(5, 3)
#define PF4   ROVARI_PIN(5, 4)
#define PF5   ROVARI_PIN(5, 5)
#define PF6   ROVARI_PIN(5, 6)
#define PF7   ROVARI_PIN(5, 7)
#define PF8   ROVARI_PIN(5, 8)
#define PF9   ROVARI_PIN(5, 9)
#define PF10  ROVARI_PIN(5, 10)
#define PF11  ROVARI_PIN(5, 11)
#define PF12  ROVARI_PIN(5, 12)
#define PF13  ROVARI_PIN(5, 13)
#define PF14  ROVARI_PIN(5, 14)

/* -- Pin modes ------------------------------------------------------ */
typedef enum {
    Output       = 0,   /* Push-pull output */
    Input        = 1,   /* Floating input */
    InputPullUp  = 2,   /* Input with internal pull-up */
    InputPullDown= 3,   /* Input with internal pull-down */
    OutputOD     = 4,   /* Open-drain output */
    AF_PushPull  = 5,   /* Alternate function push-pull */
    AF_OpenDrain = 6,   /* Alternate function open-drain */
    Analog       = 7,   /* Analog input (for ADC) */
} PinMode;

/* -- Logic levels --------------------------------------------------- */
#define High  1
#define Low   0

/* -- UART instance identifiers -------------------------------------- */
/* Only SERIAL1-3 confirmed working on V3F core.                       */
/* SERIAL4-8 have peripheral clock issues, to be investigated.         */
typedef enum {
    SERIAL1 = 1,
    SERIAL2 = 2,
    SERIAL3 = 3,
    /* SERIAL4 = 4, */   /* USART4-8: clock mismatch on V3F */
    /* SERIAL5 = 5, */
    /* SERIAL6 = 6, */
    /* SERIAL7 = 7, */
    /* SERIAL8 = 8, */
} UartInstance;

/* -- I2C instance identifiers --------------------------------------- */
typedef enum {
    I2C_1 = 1,
    I2C_2 = 2,
    I2C_3 = 3,
    I2C_4 = 4,
} I2cInstance;

/* -- SPI instance identifiers --------------------------------------- */
typedef enum {
    SPI_1 = 1,
    SPI_2 = 2,
    SPI_3 = 3,
    SPI_4 = 4,
} SpiInstance;

/* -- SPI configuration ---------------------------------------------- */
typedef struct {
    uint32_t speed;      /* Clock speed in Hz */
    uint8_t  mode;       /* SPI mode 0-3 */
    uint8_t  bit_order;  /* 0 = MSB first (default), 1 = LSB first */
} SpiConfig;

#endif /* ROVARI_DEFS_H */
