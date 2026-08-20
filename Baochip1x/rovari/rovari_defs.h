/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari - rvembedded.com
 *
 * rovari_defs.h - Pin definitions, constants, and mode enumerations
 *                 Baochip-1x variant (VexRiscv RV32IMAC, 350 MHz)
 *
 * Available GPIO:
 *   Port A: PA0 - PA15
 *   Port B: PB0 - PB15
 *   Port C: PC0 - PC15
 *   Port D: PD0 - PD15
 *   Port E: PE0 - PE15
 *   Port F: PF0 - PF15
 *
 * Pin encoding: port (bits 7:4) and pin number (bits 3:0).
 *   Port indices: 0=A, 1=B, 2=C, 3=D, 4=E, 5=F
 */

#ifndef ROVARI_DEFS_H
#define ROVARI_DEFS_H

#include <stdint.h>

/* Pin encoding */
typedef uint8_t pin_t;

#define ROVARI_PIN(port, pin)  ((uint8_t)(((port) << 4) | ((pin) & 0x0F)))
#define ROVARI_PORT(p)         (((p) >> 4) & 0x0F)
#define ROVARI_PIN_NUM(p)      ((p) & 0x0F)

/* Port A */
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

/* Port B */
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

/* Port C */
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

/* Port D */
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

/* Port E */
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

/* Port F */
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
#define PF15  ROVARI_PIN(5, 15)

/* Pin modes */
typedef enum {
    Output       = 0,
    Input        = 1,
    InputPullUp  = 2,
    InputPullDown= 3,
    OutputOD     = 4,
    AF_PushPull  = 5,
    AF_OpenDrain = 6,
    Analog       = 7,
} PinMode;

/* Logic levels */
#define High  1
#define Low   0

/* UART instance identifiers
 * Baochip-1x has UART0 through UART3 via UDMA.
 * UART2 is the default boot console. */
typedef enum {
    SERIAL1 = 0,   /* UART0 */
    SERIAL2 = 1,   /* UART1 */
    SERIAL3 = 2,   /* UART2 (boot console, default) */
    SERIAL4 = 3,   /* UART3 */
} UartInstance;

/* I2C instance identifiers
 * Baochip-1x has I2C0 through I2C3 via UDMA. */
typedef enum {
    I2C_1 = 0,
    I2C_2 = 1,
    I2C_3 = 2,
    I2C_4 = 3,
} I2cInstance;

/* SPI instance identifiers
 * Baochip-1x has SPI0 through SPI3 via UDMA.
 * SPI_1 maps to SPI2 (Dabao board: PC0=CLK, PC1=MOSI, PC2=MISO, PC3=CS). */
typedef enum {
    SPI_1 = 2,   /* SPI2 (Dabao board default) */
    SPI_2 = 0,   /* SPI0 */
    SPI_3 = 1,   /* SPI1 */
    SPI_4 = 3,   /* SPI3 */
} SpiInstance;

/* ADC constants are defined in hardware/adc.h (Dabao SDK).
 * Baochip-1x: 10-bit, 1.208V internal bandgap reference. */

#endif /* ROVARI_DEFS_H */