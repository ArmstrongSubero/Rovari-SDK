/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_st7796s.h - ST7796S 480x320 TFT LCD driver over SPI + DMA
 *
 * Hardware: ST7796S controller, SPI interface, RGB565 pixel format.
 * Uses DMA for bulk pixel transfers (lcd_push_pixels).
 *
 * Default pin mapping:
 *   SPI1_SCK  = PA5 (AF5, 3.3V domain)
 *   SPI1_MOSI = PA7 (AF5, 3.3V domain)
 *   LCD_DC    = PA0 (3.3V domain)
 *   LCD_RST   = PA1 (3.3V domain)
 *   LCD_CS    = PA3 (3.3V domain)
 *
 * Usage:
 *   #include "rovari.h"
 *   #include "rovari_st7796s.h"
 *
 *   void app_init() {
 *       lcd_init();
 *       lcd_clear(LCD_BLACK);
 *       lcd_fill_rect(10, 10, 100, 50, LCD_RED);
 *   }
 *
 *   // Bulk pixel push (for video playback, framebuffers, etc.):
 *   uint16_t strip[480 * 16];
 *   // ... render into strip ...
 *   lcd_push_pixels(x, y, 480, 16, strip);  // DMA transfer
 */

#ifndef ROVARI_ST7796S_H
#define ROVARI_ST7796S_H

#include "rovari_defs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -- Panel geometry ------------------------------------------------ */
#define LCD_PANEL_W       320     /* native portrait width */
#define LCD_PANEL_H       480     /* native portrait height */

/* After landscape rotation (default): 480 wide x 320 tall */
#define LCD_W             480
#define LCD_H             320

/* -- Common RGB565 colors ------------------------------------------ */
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

/* -- Pin configuration --------------------------------------------- */
/* Control pins (directly toggled via GPIO, not SPI)                   */
/* Override these before #include if your wiring differs.              */

/*
 * Pin assignments for CH32H417:
 *
 * IMPORTANT: PB10-PB13 are on the VIO18 (1.8V) I/O power domain
 * and cannot drive 3.3V displays. All LCD signals must use pins
 * on the VDDIO (3.3V) domain: PA0-PA7, PB15, etc.
 *
 *   SPI1_SCK  = PA5 (AF5, VDDIO domain)
 *   SPI1_MOSI = PA7 (AF5, VDDIO domain)
 *   LCD_DC    = PA0 (VDDIO domain)
 *   LCD_RST   = PA1 (VDDIO domain)
 *   LCD_CS    = PA3 (VDDIO domain)
 */

#ifndef LCD_DC_PORT
#define LCD_DC_PORT       GPIOA
#define LCD_DC_PIN        GPIO_Pin_0
#define LCD_DC_RCC        RCC_HB2Periph_GPIOA
#endif

#ifndef LCD_RST_PORT
#define LCD_RST_PORT      GPIOA
#define LCD_RST_PIN       GPIO_Pin_1
#define LCD_RST_RCC       RCC_HB2Periph_GPIOA
#endif

#ifndef LCD_CS_PORT
#define LCD_CS_PORT       GPIOA
#define LCD_CS_PIN        GPIO_Pin_3
#define LCD_CS_RCC        RCC_HB2Periph_GPIOA
#endif

/* SPI instance used for the display */
#ifndef LCD_SPI_INST
#define LCD_SPI_INST      SPI_1
#endif

/* -- Display rotation ---------------------------------------------- */
/* 0 = portrait, 1 = landscape, 2 = portrait flipped, 3 = landscape flipped */
#ifndef LCD_ROTATION
#define LCD_ROTATION      1
#endif

/* -- API ----------------------------------------------------------- */

/* Blocking API */
void lcd_init(void);
void lcd_clear(uint16_t color);
void lcd_fill_rect(uint16_t x, uint16_t y,
                   uint16_t w, uint16_t h, uint16_t color);
void lcd_push_pixels(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h,
                     uint16_t* pixels);
void lcd_push_pixels_raw(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         uint16_t* pixels);
void lcd_set_window(uint16_t x0, uint16_t y0,
                    uint16_t x1, uint16_t y1);
void lcd_write_data_16(uint16_t data);
void lcd_set_rotation(uint8_t rotation);
void lcd_set_spi_speed(uint8_t mode);
void lcd_backlight(uint8_t on);
uint16_t lcd_width(void);
uint16_t lcd_height(void);

/**
 * Non-blocking flush API for double-buffered rendering and LVGL.
 *
 * Usage (manual double buffer):
 *   lcd_flush_start(0, 0, 480, 16, buf_a);  // DMA sends buf_a
 *   // ... CPU renders into buf_b ...
 *   lcd_flush_wait();                        // wait for buf_a done
 *   lcd_flush_start(0, 16, 480, 16, buf_b); // DMA sends buf_b
 *   // ... CPU renders into buf_a ...
 *
 * Usage (LVGL flush callback):
 *   void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
 *       lcd_flush_wait();  // wait for previous
 *       lcd_flush_start(area->x1, area->y1,
 *                       area->x2 - area->x1 + 1,
 *                       area->y2 - area->y1 + 1,
 *                       (uint16_t*)px);
 *       lv_display_flush_ready(disp);  // tell LVGL buffer is free
 *   }
 */
void lcd_flush_start(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h,
                     uint16_t* pixels);
void lcd_flush_start_raw(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         uint16_t* pixels);
void lcd_flush_wait(void);
uint8_t lcd_flush_busy(void);

#ifdef __cplusplus
}
#endif

/* -- C++ convenience class ----------------------------------------- */
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
