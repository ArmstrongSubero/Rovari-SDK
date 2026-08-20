/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_ft6336u.h - FT6336U capacitive touch controller driver
 *
 * Supports 2-point multi-touch. Uses software I2C (bit-bang) to avoid
 * conflicts with rovari_i2c which manages the hardware I2C peripherals.
 *
 * Default pin mapping (matches RV-1 board):
 *   I2C_SCL = PB6   (bit-bang)
 *   I2C_SDA = PB7   (bit-bang)
 *   INT     = PA15   (touch interrupt, active low)
 *   RST     = PA8    (hardware reset, active low)
 *
 * Usage:
 *   #include "rovari.h"
 *   #include "rovari_ft6336u.h"
 *
 *   void app_init() {
 *       lcd_init();         // display first
 *       touch_init();       // then touch
 *   }
 *
 *   void app_run() {
 *       TouchData td;
 *       touch_read(&td);
 *       if (td.touched) {
 *           serial_printf("x=%d y=%d\n", td.x, td.y);
 *       }
 *   }
 */

#ifndef ROVARI_FT6336U_H
#define ROVARI_FT6336U_H

#include "rovari_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pin configuration */
/* Override before #include if your wiring differs                     */

#ifndef TOUCH_SCL_PORT
#define TOUCH_SCL_PORT      GPIOB
#define TOUCH_SCL_PIN       GPIO_Pin_6
#endif

#ifndef TOUCH_SDA_PORT
#define TOUCH_SDA_PORT      GPIOB
#define TOUCH_SDA_PIN       GPIO_Pin_7
#endif

#ifndef TOUCH_INT_PORT
#define TOUCH_INT_PORT      GPIOA
#define TOUCH_INT_PIN       GPIO_Pin_15
#define TOUCH_INT_RCC       RCC_APB2Periph_GPIOA
#endif

#ifndef TOUCH_RST_PORT
#define TOUCH_RST_PORT      GPIOA
#define TOUCH_RST_PIN       GPIO_Pin_8
#define TOUCH_RST_RCC       RCC_APB2Periph_GPIOA
#endif

/* I2C GPIO clock (both SCL and SDA assumed on same port) */
#ifndef TOUCH_I2C_GPIO_RCC
#define TOUCH_I2C_GPIO_RCC  RCC_APB2Periph_GPIOB
#endif

/* FT6336U I2C address (7-bit) */
#define FT6336U_ADDR        0x38

/* TouchData is defined in rovari_touch.h */

/* API */

/**
 * Initialize the FT6336U touch controller.
 * Configures I2C GPIO pins (bit-bang), INT pin, RST pin.
 * Performs hardware reset and reads chip ID.
 *
 * @return true if chip responded, false on I2C failure
 */
bool touch_init(void);

/**
 * Read current touch state (raw panel coordinates).
 * Performs a single I2C burst read (11 bytes) to get both touch points.
 * If no touch is active, data->touched is false and coordinates are zero.
 *
 * Coordinates are in the panel's native portrait orientation (0-319 x 0-479)
 * regardless of display rotation. For screen-mapped coordinates, use
 * touch_read_lcd() instead.
 *
 * @param data  Output structure filled with touch data
 */
void touch_read(TouchData* data);

/**
 * Read touch state with coordinates mapped to current LCD orientation.
 *
 * Applies the rotation transform so coordinates match pixel positions:
 *   Rotation 0 (portrait):         x = raw_x,         y = raw_y
 *   Rotation 1 (landscape):        x = raw_y,         y = 319 - raw_x
 *   Rotation 2 (portrait flipped): x = 319 - raw_x,   y = 479 - raw_y
 *   Rotation 3 (landscape flipped): x = 479 - raw_y,  y = raw_x
 *
 * The rotation used is whatever was last set by lcd_set_rotation() or
 * lcd_init(). Call touch_set_rotation() to override manually.
 *
 * @param data  Output structure filled with screen-mapped touch data
 */
void touch_read_lcd(TouchData* data);

/**
 * Set the touch coordinate rotation.
 * Normally this is set automatically when you call lcd_init() or
 * lcd_set_rotation(). Only call this if you need a custom mapping.
 *
 * @param rotation  0-3, matching the LCD rotation value
 */
void touch_set_rotation(uint8_t rotation);

/**
 * Read the FT6336U chip ID register.
 * Expected values: 0x64 (FT6336U) or 0x00 (some variants).
 *
 * @return chip ID byte
 */
uint8_t touch_read_chip_id(void);

#ifdef __cplusplus
}
#endif

/* C++ convenience class */
#ifdef __cplusplus

class Touch {
public:
    Touch() {}

    bool begin() { return touch_init(); }

    /** Read raw panel coordinates. */
    bool read(TouchData* data) {
        touch_read(data);
        return data->touched;
    }

    /** Read screen-mapped coordinates (accounts for LCD rotation). */
    bool readLcd(TouchData* data) {
        touch_read_lcd(data);
        return data->touched;
    }

    /** Quick check: is a finger touching? */
    bool pressed() {
        TouchData td;
        touch_read(&td);
        return td.touched;
    }

    uint8_t chipId() { return touch_read_chip_id(); }
};

#endif /* __cplusplus */

#endif /* ROVARI_FT6336U_H */
