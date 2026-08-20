/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_ft6336u.h - FT6336U capacitive touch controller (CH32H417)
 *
 * Default: Uses software (bit-bang) I2C via rovari_swi2c. All pins
 * must be on the VDDIO (3.3V) I/O domain. PB6/PB7 (default I2C1
 * pins) are on VIO18 and will NOT work with 3.3V touch controllers.
 *
 * Optional: Define TOUCH_USE_HW_I2C before including this header to
 * use hardware I2C instead. Set TOUCH_I2C_INSTANCE to the desired
 * I2C peripheral (default: I2C_2 on PC0/PC1).
 *
 * Default pin mapping (3.3V domain):
 *   I2C_SCL = PA2  (software I2C, VDDIO)
 *   I2C_SDA = PB5  (software I2C, VDDIO) - frees PA4 for DAC1
 *   INT     = PA6  (touch interrupt, active low)
 *   RST     = PA8  (hardware reset, active low)
 *
 * Usage:
 *   #include "rovari.h"
 *
 *   void app_init() {
 *       lcd_init();
 *       touch_init();
 *   }
 *
 *   void app_run() {
 *       TouchData td;
 *       touch_read_lcd(&td);
 *       if (td.touched) {
 *           printf("x=%d y=%d\r\n", td.x, td.y);
 *       }
 *   }
 */

#ifndef ROVARI_FT6336U_H
#define ROVARI_FT6336U_H

#include "rovari_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -- Pin configuration --------------------------------------------- */
/* All pins MUST be on the VDDIO (3.3V) I/O domain.                   */
/* Override before #include if your wiring differs.                    */

#ifndef TOUCH_SCL_PORT
#define TOUCH_SCL_PORT      GPIOA
#define TOUCH_SCL_PIN       GPIO_Pin_2
#endif

#ifndef TOUCH_SDA_PORT
#define TOUCH_SDA_PORT      GPIOB
#define TOUCH_SDA_PIN       GPIO_Pin_5
#endif

#ifndef TOUCH_INT_PORT
#define TOUCH_INT_PORT      GPIOA
#define TOUCH_INT_PIN       GPIO_Pin_6
#define TOUCH_INT_RCC       RCC_HB2Periph_GPIOA
#endif

#ifndef TOUCH_RST_PORT
#define TOUCH_RST_PORT      GPIOA
#define TOUCH_RST_PIN       GPIO_Pin_8
#define TOUCH_RST_RCC       RCC_HB2Periph_GPIOA
#endif

/* I2C GPIO clock (used by software I2C path) */
#ifndef TOUCH_I2C_GPIO_RCC
#define TOUCH_I2C_GPIO_RCC  (RCC_HB2Periph_GPIOA | RCC_HB2Periph_GPIOB)
#endif

/* Hardware I2C instance (only used when TOUCH_USE_HW_I2C is defined) */
#ifndef TOUCH_I2C_INSTANCE
#define TOUCH_I2C_INSTANCE  I2C_1
#endif

/* FT6336U I2C address (7-bit) */
#define FT6336U_ADDR        0x38

/* -- API ----------------------------------------------------------- */

/**
 * Initialize the FT6336U touch controller.
 * Configures I2C (software or hardware), INT pin, RST pin.
 * Performs hardware reset and reads chip ID.
 *
 * @return true if chip responded, false on I2C failure
 */
bool touch_init(void);

/**
 * Read current touch state (raw panel coordinates).
 * Coordinates are in the panel's native portrait orientation (0-319 x 0-479)
 * regardless of display rotation.
 *
 * @param data  Output structure filled with touch data
 */
void touch_read(TouchData* data);

/**
 * Read touch state with coordinates mapped to current LCD orientation.
 *
 * @param data  Output structure filled with screen-mapped touch data
 */
void touch_read_lcd(TouchData* data);

/**
 * Set the touch coordinate rotation.
 * Normally set automatically by lcd_set_rotation().
 *
 * @param rotation  0-3, matching the LCD rotation value
 */
void touch_set_rotation(uint8_t rotation);

/**
 * Read the FT6336U chip ID register.
 * Expected: 0x64 (FT6336U) or 0x00 (some variants).
 */
uint8_t touch_read_chip_id(void);

#ifdef __cplusplus
}
#endif

/* -- C++ convenience class ----------------------------------------- */
#ifdef __cplusplus

class Touch {
public:
    Touch() {}

    bool begin() { return touch_init(); }

    bool read(TouchData* data) {
        touch_read(data);
        return data->touched;
    }

    bool readLcd(TouchData* data) {
        touch_read_lcd(data);
        return data->touched;
    }

    bool pressed() {
        TouchData td;
        touch_read(&td);
        return td.touched;
    }

    uint8_t chipId() { return touch_read_chip_id(); }
};

#endif /* __cplusplus */

#endif /* ROVARI_FT6336U_H */
