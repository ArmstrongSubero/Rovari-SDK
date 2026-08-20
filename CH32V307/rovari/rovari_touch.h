/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_touch.h - Common touch controller interface
 *
 * Display drivers include this header (not a specific touch driver)
 * to optionally sync rotation. If no touch driver is linked, the
 * weak default stubs do nothing.
 *
 * Touch driver implementations (FT6336U, XPT2046, GT911, etc.)
 * override these weak functions with their real implementations.
 *
 * Users include the specific touch driver header they need:
 *   #include "rovari_ft6336u.h"   // capacitive, I2C
 *   #include "rovari_xpt2046.h"   // resistive, SPI (future)
 *
 * The TouchData struct is defined here so all touch drivers share
 * the same data format.
 */

#ifndef ROVARI_TOUCH_H
#define ROVARI_TOUCH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common touch data structure */
typedef struct {
    uint8_t  num_touches;   /* 0, 1, or 2 */
    bool     touched;       /* true if at least 1 finger down */
    uint16_t x;             /* point 1 X coordinate */
    uint16_t y;             /* point 1 Y coordinate */
    uint8_t  id;            /* point 1 touch ID (stable per finger) */
    uint16_t x2;            /* point 2 X (valid when num_touches >= 2) */
    uint16_t y2;            /* point 2 Y (valid when num_touches >= 2) */
    uint8_t  id2;           /* point 2 touch ID */
} TouchData;

/* Common touch API (weak defaults, overridden by drivers) */

/**
 * @brief Set the touch panel rotation to match the display.
 *
 * Called automatically by display drivers when rotation changes.
 * If no touch driver is linked, this is a no-op.
 *
 * @param[in] rotation 0-3, matching the LCD rotation value
 */
void touch_set_rotation(uint8_t rotation);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_TOUCH_H */
