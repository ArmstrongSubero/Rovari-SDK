/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_ssd1306.h - SSD1306 I2C OLED driver API (128x64 and 128x32)
 *
 * Two implementations share this header:
 *   rovari_ssd1306.c      Full driver: RAM framebuffer, drawing, display() flush
 *   rovari_ssd1306_lite.c Lite driver: no buffer, text and icons write directly
 *
 * Pick ONE in your project. The API is the same; drawing functions are
 * simply no-ops in the lite version.
 *
 * Full driver usage:
 *   Oled oled(I2C_1, OLED_128x64);
 *   oled.begin();
 *   oled.circle(64, 32, 15, OLED_COLOR_WHITE);
 *   oled.printLine(0, "Hello!");
 *   oled.display();
 *
 * Lite driver usage:
 *   Oled oled(I2C_1, OLED_128x32);
 *   oled.begin();
 *   oled.printLine(0, "Hello!");   // writes immediately to display
 */

#ifndef ROVARI_SSD1306_H
#define ROVARI_SSD1306_H

#include "rovari_defs.h"
#include "rovari_i2c.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 *  Constants and types
 * ----------------------------------------------------------------------- */

#define SSD1306_WIDTH        128
#define SSD1306_MAX_HEIGHT    64
#define SSD1306_MAX_PAGES      8
#define SSD1306_I2C_ADDR    0x3C

typedef enum {
    OLED_128x32 = 32,
    OLED_128x64 = 64
} OledSize;

typedef enum {
    OLED_ORIENT_0 = 0,
    OLED_ORIENT_180,
    OLED_MIRROR_X,
    OLED_MIRROR_Y
} OledOrient;

typedef enum {
    OLED_COLOR_BLACK = 0,
    OLED_COLOR_WHITE = 1,
    OLED_COLOR_INVERT = 2
} OledColor;

/* -----------------------------------------------------------------------
 *  C API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

/* Init / control */
void oled_init(I2cInstance inst, OledSize size, uint8_t addr);
void oled_off(void);
void oled_on(void);
void oled_contrast(uint8_t val);
void oled_set_orientation(OledOrient orient);
void oled_invert(uint8_t inv);

/* Clear */
void oled_clear(void);
void oled_clear_line(uint8_t page);

/* Flush (full driver only; no-op in lite) */
void oled_display(void);
void oled_update_line(uint8_t page);
void oled_update_partial(uint8_t page, uint8_t start_col, uint8_t num_cols);

/* Drawing (full driver only; no-op in lite) */
void oled_pixel(int16_t x, int16_t y, OledColor color);
void oled_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, OledColor color);
void oled_rect(int16_t x, int16_t y, int16_t w, int16_t h, OledColor color);
void oled_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, OledColor color);
void oled_circle(int16_t cx, int16_t cy, int16_t r, OledColor color);
void oled_hline(int16_t x, int16_t y, int16_t w, OledColor color);
void oled_vline(int16_t x, int16_t y, int16_t h, OledColor color);

/* Text (works in both) */
void oled_put_char(char ch, uint8_t x, uint8_t page);
void oled_write_string(const char* s, uint8_t page, uint8_t col_char);
void oled_printf_line(uint8_t line, const char* fmt, ...);

/* Icons / bitmaps (work in both) */
void oled_draw_battery(uint8_t percentage, uint8_t x, uint8_t page);
void oled_draw_bitmap(const uint8_t* data, uint8_t x, uint8_t page,
                       uint8_t w, uint8_t h_pages);

/* Info */
uint8_t  oled_get_height(void);
uint8_t  oled_get_pages(void);
uint8_t* oled_get_buffer(void);   /* returns NULL in lite driver */

extern const uint8_t OledFont[][8];
extern const uint8_t BatteryIcon[];

#ifdef __cplusplus
}
#endif

/* -----------------------------------------------------------------------
 *  C++ API
 * ----------------------------------------------------------------------- */
#ifdef __cplusplus

#include <cstdarg>
#include <cstdio>

class Oled {
public:
    explicit Oled(I2cInstance inst, OledSize size = OLED_128x64,
                  uint8_t addr = SSD1306_I2C_ADDR)
        : _inst(inst), _size(size), _addr(addr) {}

    void begin()                      { oled_init(_inst, _size, _addr); }
    void off()                        { oled_off(); }
    void on()                         { oled_on(); }
    void contrast(uint8_t v)          { oled_contrast(v); }
    void setOrientation(OledOrient o) { oled_set_orientation(o); }
    void invert(uint8_t inv)          { oled_invert(inv); }

    void clear()                      { oled_clear(); }
    void clearLine(uint8_t page)      { oled_clear_line(page); }
    void display()                    { oled_display(); }
    void updateLine(uint8_t page)     { oled_update_line(page); }

    /* Drawing (full driver only) */
    void pixel(int16_t x, int16_t y, OledColor c = OLED_COLOR_WHITE) { oled_pixel(x, y, c); }
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, OledColor c = OLED_COLOR_WHITE) { oled_line(x0, y0, x1, y1, c); }
    void rect(int16_t x, int16_t y, int16_t w, int16_t h, OledColor c = OLED_COLOR_WHITE) { oled_rect(x, y, w, h, c); }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, OledColor c = OLED_COLOR_WHITE) { oled_fill_rect(x, y, w, h, c); }
    void circle(int16_t cx, int16_t cy, int16_t r, OledColor c = OLED_COLOR_WHITE) { oled_circle(cx, cy, r, c); }
    void hline(int16_t x, int16_t y, int16_t w, OledColor c = OLED_COLOR_WHITE) { oled_hline(x, y, w, c); }
    void vline(int16_t x, int16_t y, int16_t h, OledColor c = OLED_COLOR_WHITE) { oled_vline(x, y, h, c); }

    /* Text (both) */
    void putChar(char ch, uint8_t x, uint8_t page) { oled_put_char(ch, x, page); }
    void writeString(const char* s, uint8_t page, uint8_t col_char) { oled_write_string(s, page, col_char); }
    void printLine(uint8_t line, const char* fmt, ...) __attribute__((format(printf, 3, 4))) {
        char buf[17];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        oled_printf_line(line, "%s", buf);
    }

    /* Icons (both) */
    void drawBattery(uint8_t pct, uint8_t x, uint8_t page) { oled_draw_battery(pct, x, page); }
    void drawBitmap(const uint8_t* data, uint8_t x, uint8_t page, uint8_t w, uint8_t h_pages) { oled_draw_bitmap(data, x, page, w, h_pages); }

    uint8_t* getBuffer()   { return oled_get_buffer(); }
    uint8_t height() const { return (uint8_t)_size; }
    uint8_t pages() const  { return (uint8_t)_size / 8; }

private:
    I2cInstance _inst;
    OledSize    _size;
    uint8_t     _addr;
};

#endif /* __cplusplus */
#endif /* ROVARI_SSD1306_H */
