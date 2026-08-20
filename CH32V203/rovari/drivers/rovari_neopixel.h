/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Rovari
 *
 * rovari_neopixel.h - WS2812B NeoPixel driver (GPIO bit-bang)
 *
 * Usage:
 *   neo_init(PA4, 1);              // pin, number of LEDs
 *   neo_set(0, 255, 0, 0);         // pixel 0 = red
 *   neo_show();                    // send data to strip
 *   neo_clear();                   // all off
 *   neo_set_hsv(0, 0, 255, 128);   // pixel 0 = red at 50% brightness
 *   neo_show();
 */

#ifndef ROVARI_NEOPIXEL_H
#define ROVARI_NEOPIXEL_H

#include "rovari_defs.h"

#define NEO_MAX_PIXELS 16

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the NeoPixel pin and set pixel count.
 * Configures the pin as push-pull output and clears the buffer.
 *
 * @param pin       GPIO pin (e.g. PA4)
 * @param count     Number of LEDs in the strip (max NEO_MAX_PIXELS)
 */
void neo_init(pin_t pin, uint8_t count);

/**
 * Set a pixel's color in the buffer. Does not send data.
 * Call neo_show() to update the strip.
 */
void neo_set(uint8_t pixel, uint8_t r, uint8_t g, uint8_t b);

/**
 * Set a pixel's color using HSV. Hue 0-191, sat ignored (full), val 0-255.
 */
void neo_set_hsv(uint8_t pixel, uint8_t hue, uint8_t val);

/**
 * Clear all pixels in the buffer (set to black).
 * Call neo_show() to update the strip.
 */
void neo_clear(void);

/**
 * Set all pixels to the same color.
 * Call neo_show() to update the strip.
 */
void neo_fill(uint8_t r, uint8_t g, uint8_t b);

/**
 * Send the buffer contents to the NeoPixel strip.
 * Temporarily disables interrupts during transmission (~30us per pixel).
 */
void neo_show(void);

/**
 * Get the configured pixel count.
 */
uint8_t neo_count(void);

#ifdef __cplusplus
}
#endif

#endif /* ROVARI_NEOPIXEL_H */
