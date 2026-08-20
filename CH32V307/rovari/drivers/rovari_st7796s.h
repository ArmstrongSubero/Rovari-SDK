/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_st7796s.h - ST7796S 480x320 TFT LCD driver over SPI + DMA
 *
 * Hardware: ST7796S controller, SPI interface, RGB565 pixel format.
 * Uses DMA for bulk pixel transfers (LCD_PushPixels).
 *
 * Default pin mapping (matches RV-1 board / WCH EVT):
 *   SPI2_SCK  = PB13
 *   SPI2_MOSI = PB15
 *   LCD_CS    = PB12  (software chip select)
 *   LCD_DC    = PB10  (data/command select)
 *   LCD_RST   = PB11  (hardware reset)
 *   LCD_LED   = PB9   (backlight, active high)
 *
 * Usage:
 *   #include "rovari.h"
 *   #include "rovari_st7796s.h"
 *
 *   void app_init() {
 *       lcd_init();                         // SPI2 + DMA + GPIO + ST7796S init
 *       lcd_clear(LCD_BLACK);               // fill screen
 *       lcd_fill_rect(10, 10, 100, 50, LCD_RED);
 *   }
 *
 *   // Bulk pixel push (for game engines, framebuffers, etc.):
 *   uint16_t strip[240 * 16];              // RGB565 pixels
 *   // ... render into strip ...
 *   lcd_push_pixels(x, y, 240, 16, strip); // DMA transfer
 *
 * To change pin assignments, edit the LCD_xxx_PIN / LCD_xxx_PORT
 * defines below before including this header, or modify them here.
 */

#ifndef ROVARI_ST7796S_H
#define ROVARI_ST7796S_H

#include "rovari_defs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Panel geometry */
#define LCD_PANEL_W       320     /* native portrait width */
#define LCD_PANEL_H       480     /* native portrait height */

/* After landscape rotation (default): 480 wide x 320 tall */
#define LCD_W             480
#define LCD_H             320

/* Common RGB565 colors */
#define LCD_WHITE         0xFFFF
#define LCD_BLACK         0x0000
#define LCD_BLUE          0x001F
#define LCD_RED           0xF800
#define LCD_GREEN         0x07E0
#define LCD_CYAN          0x07FF
#define LCD_YELLOW        0xFFE0
#define LCD_MAGENTA       0xF81F
#define LCD_GRAY          0x8430
#define LCD_ORANGE        0xFD20
#define LCD_DARKBLUE      0x0011

/* Pin configuration */
/* Control pins (directly toggled via GPIO, not SPI)                   */
/* Override these before #include if your wiring differs.              */

#ifndef LCD_LED_PORT
#define LCD_LED_PORT      GPIOB
#define LCD_LED_PIN       GPIO_Pin_9
#define LCD_LED_RCC       RCC_APB2Periph_GPIOB
#endif

#ifndef LCD_DC_PORT
#define LCD_DC_PORT       GPIOB
#define LCD_DC_PIN        GPIO_Pin_10
#define LCD_DC_RCC        RCC_APB2Periph_GPIOB
#endif

#ifndef LCD_RST_PORT
#define LCD_RST_PORT      GPIOB
#define LCD_RST_PIN       GPIO_Pin_11
#define LCD_RST_RCC       RCC_APB2Periph_GPIOB
#endif

#ifndef LCD_CS_PORT
#define LCD_CS_PORT       GPIOB
#define LCD_CS_PIN        GPIO_Pin_12
#define LCD_CS_RCC        RCC_APB2Periph_GPIOB
#endif

/* SPI instance used for the display */
#ifndef LCD_SPI_INST
#define LCD_SPI_INST      SPI_2
#endif

/* Display rotation */
/* 0 = portrait, 1 = landscape, 2 = portrait flipped, 3 = landscape flipped */
#ifndef LCD_ROTATION
#define LCD_ROTATION      1
#endif

/* API */

/**
 * Initialize the ST7796S display.
 * Configures SPI2 at maximum speed, sets up DMA, initializes control
 * GPIOs, performs hardware reset, sends the ST7796S init sequence,
 * and sets landscape rotation.
 *
 * Call this once in app_init().
 */
void lcd_init(void);

/**
 * Fill the entire screen with a solid color.
 * @param color  RGB565 color value
 */
void lcd_clear(uint16_t color);

/**
 * Fill a rectangle with a solid color.
 * @param x,y    Top-left corner
 * @param w,h    Width and height in pixels
 * @param color  RGB565 color value
 */
void lcd_fill_rect(uint16_t x, uint16_t y,
                   uint16_t w, uint16_t h, uint16_t color);

/**
 * Push a buffer of RGB565 pixels to a rectangular region via DMA.
 *
 * This is the high-performance path for game engines, framebuffers,
 * camera output, etc. Pixels are in native uint16_t (little-endian);
 * the driver handles byte-swapping for the SPI bus.
 *
 * WARNING: This function modifies the pixel buffer in-place for
 * byte-swap. If you need the original data preserved, copy first.
 *
 * For buffers larger than 65534 bytes the driver automatically
 * splits into multiple DMA chunks.
 *
 * @param x,y      Top-left corner of the target region
 * @param w,h      Width and height of the region
 * @param pixels   Buffer of w*h RGB565 pixels (modified in-place)
 */
void lcd_push_pixels(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h,
                     uint16_t* pixels);

/**
 * Set the display window for subsequent pixel writes.
 * After calling this, write pixel data with lcd_write_data_16().
 *
 * @param x0,y0  Start column/row
 * @param x1,y1  End column/row (inclusive)
 */
void lcd_set_window(uint16_t x0, uint16_t y0,
                    uint16_t x1, uint16_t y1);

/**
 * Write a single 16-bit pixel (blocking, no DMA).
 * Useful for drawing individual pixels or small primitives.
 */
void lcd_write_data_16(uint16_t data);

/**
 * Set the display rotation / scan direction.
 * @param rotation  0-3 (portrait, landscape, portrait flip, landscape flip)
 */
void lcd_set_rotation(uint8_t rotation);

/**
 * Turn the backlight on or off.
 * @param on  1 = backlight on, 0 = off
 */
void lcd_backlight(uint8_t on);

/**
 * Get current display width (depends on rotation).
 */
uint16_t lcd_width(void);

/**
 * Get current display height (depends on rotation).
 */
uint16_t lcd_height(void);

#ifdef __cplusplus
}
#endif

/* C++ convenience class */
#ifdef __cplusplus

class Lcd {
public:
    Lcd() {}

    void begin()                    { lcd_init(); }
    void clear(uint16_t c)         { lcd_clear(c); }
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c) {
        lcd_fill_rect(x, y, w, h, c);
    }
    void pushPixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t* px) {
        lcd_push_pixels(x, y, w, h, px);
    }
    void backlight(bool on)        { lcd_backlight(on ? 1 : 0); }
    void setRotation(uint8_t r)    { lcd_set_rotation(r); }
    uint16_t width() const         { return lcd_width(); }
    uint16_t height() const        { return lcd_height(); }
};

#endif /* __cplusplus */

#endif /* ROVARI_ST7796S_H */
