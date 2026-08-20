/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_ssd1306.c
 * @brief SSD1306 OLED full driver: framebuffer + drawing.
 *
 * RAM cost ~1.3 KB (1024-byte framebuffer + tx buffer + text cache). Use on
 * chips with RAM to spare; for constrained chips use rovari_ssd1306_lite.c.
 * Integer-only graphics math.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_ssd1306.h"

/* Internal state */

static I2cInstance oled_i2c;
static uint8_t     oled_addr;
static uint8_t     oled_height;
static uint8_t     oled_pages;
static uint8_t     screen_buffer[SSD1306_MAX_PAGES][SSD1306_WIDTH];
static uint8_t     tx_buf[1 + SSD1306_WIDTH];
static char        last_text[SSD1306_MAX_PAGES][17];

/* SSD1306 commands */

#define CMD_DISPLAY_OFF           0xAE
#define CMD_DISPLAY_ON            0xAF
#define CMD_SET_CONTRAST          0x81
#define CMD_DISPLAY_ALL_ON_RESUME 0xA4
#define CMD_NORMAL_DISPLAY        0xA6
#define CMD_INVERT_DISPLAY        0xA7
#define CMD_SET_DISPLAY_OFFSET    0xD3
#define CMD_SET_COM_PINS          0xDA
#define CMD_SET_VCOM_DETECT       0xDB
#define CMD_SET_DISPLAY_CLOCK_DIV 0xD5
#define CMD_SET_PRECHARGE         0xD9
#define CMD_SET_MULTIPLEX         0xA8
#define CMD_SET_START_LINE        0x40
#define CMD_MEMORY_MODE           0x20
#define CMD_SEG_REMAP             0xA0
#define CMD_COM_SCAN_INC          0xC0
#define CMD_COM_SCAN_DEC          0xC8
#define CMD_CHARGE_PUMP           0x8D
#define CMD_DEACTIVATE_SCROLL     0x2E

/* Font */

const uint8_t OledFont[][8] =
{
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x5F,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x07,0x00,0x07,0x00,0x00,0x00},
  {0x00,0x14,0x7F,0x14,0x7F,0x14,0x00,0x00},
  {0x00,0x24,0x2A,0x7F,0x2A,0x12,0x00,0x00},
  {0x00,0x23,0x13,0x08,0x64,0x62,0x00,0x00},
  {0x00,0x36,0x49,0x55,0x22,0x50,0x00,0x00},
  {0x00,0x00,0x05,0x03,0x00,0x00,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00,0x00,0x00,0x00},
  {0x00,0x41,0x22,0x1C,0x00,0x00,0x00,0x00},
  {0x00,0x08,0x2A,0x1C,0x2A,0x08,0x00,0x00},
  {0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00},
  {0x00,0xA0,0x60,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x08,0x08,0x08,0x08,0x08,0x00,0x00},
  {0x00,0x60,0x60,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x20,0x10,0x08,0x04,0x02,0x00,0x00},
  {0x00,0x3E,0x51,0x49,0x45,0x3E,0x00,0x00},
  {0x00,0x00,0x42,0x7F,0x40,0x00,0x00,0x00},
  {0x00,0x62,0x51,0x49,0x49,0x46,0x00,0x00},
  {0x00,0x22,0x41,0x49,0x49,0x36,0x00,0x00},
  {0x00,0x18,0x14,0x12,0x7F,0x10,0x00,0x00},
  {0x00,0x27,0x45,0x45,0x45,0x39,0x00,0x00},
  {0x00,0x3C,0x4A,0x49,0x49,0x30,0x00,0x00},
  {0x00,0x01,0x71,0x09,0x05,0x03,0x00,0x00},
  {0x00,0x36,0x49,0x49,0x49,0x36,0x00,0x00},
  {0x00,0x06,0x49,0x49,0x29,0x1E,0x00,0x00},
  {0x00,0x00,0x36,0x36,0x00,0x00,0x00,0x00},
  {0x00,0x00,0xAC,0x6C,0x00,0x00,0x00,0x00},
  {0x00,0x08,0x14,0x22,0x41,0x00,0x00,0x00},
  {0x00,0x14,0x14,0x14,0x14,0x14,0x00,0x00},
  {0x00,0x41,0x22,0x14,0x08,0x00,0x00,0x00},
  {0x00,0x02,0x01,0x51,0x09,0x06,0x00,0x00},
  {0x00,0x32,0x49,0x79,0x41,0x3E,0x00,0x00},
  {0x00,0x7E,0x09,0x09,0x09,0x7E,0x00,0x00},
  {0x00,0x7F,0x49,0x49,0x49,0x36,0x00,0x00},
  {0x00,0x3E,0x41,0x41,0x41,0x22,0x00,0x00},
  {0x00,0x7F,0x41,0x41,0x22,0x1C,0x00,0x00},
  {0x00,0x7F,0x49,0x49,0x49,0x41,0x00,0x00},
  {0x00,0x7F,0x09,0x09,0x09,0x01,0x00,0x00},
  {0x00,0x3E,0x41,0x41,0x51,0x72,0x00,0x00},
  {0x00,0x7F,0x08,0x08,0x08,0x7F,0x00,0x00},
  {0x00,0x41,0x7F,0x41,0x00,0x00,0x00,0x00},
  {0x00,0x20,0x40,0x41,0x3F,0x01,0x00,0x00},
  {0x00,0x7F,0x08,0x14,0x22,0x41,0x00,0x00},
  {0x00,0x7F,0x40,0x40,0x40,0x40,0x00,0x00},
  {0x00,0x7F,0x02,0x0C,0x02,0x7F,0x00,0x00},
  {0x00,0x7F,0x04,0x08,0x10,0x7F,0x00,0x00},
  {0x00,0x3E,0x41,0x41,0x41,0x3E,0x00,0x00},
  {0x00,0x7F,0x09,0x09,0x09,0x06,0x00,0x00},
  {0x00,0x3E,0x41,0x51,0x21,0x5E,0x00,0x00},
  {0x00,0x7F,0x09,0x19,0x29,0x46,0x00,0x00},
  {0x00,0x26,0x49,0x49,0x49,0x32,0x00,0x00},
  {0x00,0x01,0x01,0x7F,0x01,0x01,0x00,0x00},
  {0x00,0x3F,0x40,0x40,0x40,0x3F,0x00,0x00},
  {0x00,0x1F,0x20,0x40,0x20,0x1F,0x00,0x00},
  {0x00,0x3F,0x40,0x38,0x40,0x3F,0x00,0x00},
  {0x00,0x63,0x14,0x08,0x14,0x63,0x00,0x00},
  {0x00,0x03,0x04,0x78,0x04,0x03,0x00,0x00},
  {0x00,0x61,0x51,0x49,0x45,0x43,0x00,0x00},
  {0x00,0x7F,0x41,0x41,0x00,0x00,0x00,0x00},
  {0x00,0x02,0x04,0x08,0x10,0x20,0x00,0x00},
  {0x00,0x41,0x41,0x7F,0x00,0x00,0x00,0x00},
  {0x00,0x04,0x02,0x01,0x02,0x04,0x00,0x00},
  {0x00,0x80,0x80,0x80,0x80,0x80,0x00,0x00},
  {0x00,0x01,0x02,0x04,0x00,0x00,0x00,0x00},
  {0x00,0x20,0x54,0x54,0x54,0x78,0x00,0x00},
  {0x00,0x7F,0x48,0x44,0x44,0x38,0x00,0x00},
  {0x00,0x38,0x44,0x44,0x28,0x00,0x00,0x00},
  {0x00,0x38,0x44,0x44,0x48,0x7F,0x00,0x00},
  {0x00,0x38,0x54,0x54,0x54,0x18,0x00,0x00},
  {0x00,0x08,0x7E,0x09,0x02,0x00,0x00,0x00},
  {0x00,0x18,0xA4,0xA4,0xA4,0x7C,0x00,0x00},
  {0x00,0x7F,0x08,0x04,0x04,0x78,0x00,0x00},
  {0x00,0x00,0x7D,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x80,0x84,0x7D,0x00,0x00,0x00,0x00},
  {0x00,0x7F,0x10,0x28,0x44,0x00,0x00,0x00},
  {0x00,0x41,0x7F,0x40,0x00,0x00,0x00,0x00},
  {0x00,0x7C,0x04,0x18,0x04,0x78,0x00,0x00},
  {0x00,0x7C,0x08,0x04,0x7C,0x00,0x00,0x00},
  {0x00,0x38,0x44,0x44,0x38,0x00,0x00,0x00},
  {0x00,0xFC,0x24,0x24,0x18,0x00,0x00,0x00},
  {0x00,0x18,0x24,0x24,0xFC,0x00,0x00,0x00},
  {0x00,0x00,0x7C,0x08,0x04,0x00,0x00,0x00},
  {0x00,0x48,0x54,0x54,0x24,0x00,0x00,0x00},
  {0x00,0x04,0x7F,0x44,0x00,0x00,0x00,0x00},
  {0x00,0x3C,0x40,0x40,0x7C,0x00,0x00,0x00},
  {0x00,0x1C,0x20,0x40,0x20,0x1C,0x00,0x00},
  {0x00,0x3C,0x40,0x30,0x40,0x3C,0x00,0x00},
  {0x00,0x44,0x28,0x10,0x28,0x44,0x00,0x00},
  {0x00,0x1C,0xA0,0xA0,0x7C,0x00,0x00,0x00},
  {0x00,0x44,0x64,0x54,0x4C,0x44,0x00,0x00},
  {0x00,0x08,0x36,0x41,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x7F,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x41,0x36,0x08,0x00,0x00,0x00,0x00},
  {0x00,0x02,0x01,0x01,0x02,0x01,0x00,0x00},
  {0x00,0x02,0x05,0x05,0x02,0x00,0x00,0x00},
};

const uint8_t BatteryIcon[10] =
{
    0xFF, 0x81, 0xBD, 0xBD, 0xBD, 0x81, 0xFF, 0x18, 0x00, 0x00
};

/* Low-level I2C */

/**
 * @brief Send a command byte to the panel.
 * @param[in] cmd Command byte.
 */
static void oled_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    i2c_write_raw(oled_i2c, oled_addr, buf, 2);
}

/**
 * @brief Send a data run to the panel (clamped to one row width).
 * @param[in] data Data bytes.
 * @param[in] len  Byte count.
 */
static void oled_data(const uint8_t* data, uint16_t len)
{
    SEVS_REQUIRE_NOT_NULL(data);
    if (len > SSD1306_WIDTH) len = SSD1306_WIDTH;
    tx_buf[0] = 0x40;
    memcpy(&tx_buf[1], data, len);
    i2c_write_raw(oled_i2c, oled_addr, tx_buf, len + 1);
}

/**
 * @brief Set the panel write position.
 * @param[in] page Page index.
 * @param[in] col  Column.
 */
static void oled_set_pos(uint8_t page, uint8_t col)
{
    oled_cmd(0xB0 | (page & 0x07));
    oled_cmd(0x00 | (col & 0x0F));
    oled_cmd(0x10 | ((col >> 4) & 0x0F));
}

/**
 * @brief Map non-printable characters to a space.
 * @param[in] ch Input character.
 * @return Printable character or space.
 */
static inline char sanitize_char(char ch)
{
    if ((unsigned char)ch < 32 || (unsigned char)ch > 127) return ' ';
    return ch;
}

/* Init */

/**
 * @brief Initialize the OLED panel and clear it.
 * @param[in] inst I2C instance.
 * @param[in] size Panel height variant.
 * @param[in] addr I2C address.
 * @req REQ-ROVARI-SSD1306-0010
 */
void oled_init(I2cInstance inst, OledSize size, uint8_t addr)
{
    oled_i2c    = inst;
    oled_addr   = addr;
    oled_height = (uint8_t)size;
    oled_pages  = oled_height / 8;
    SEVS_INVARIANT(oled_pages <= SSD1306_MAX_PAGES);
    memset(last_text, 0, sizeof(last_text));

    Delay_Ms(10);
    oled_cmd(CMD_DISPLAY_OFF);
    oled_cmd(CMD_DEACTIVATE_SCROLL);
    oled_cmd(CMD_SET_DISPLAY_CLOCK_DIV); oled_cmd(0x80);
    oled_cmd(CMD_SET_MULTIPLEX);         oled_cmd(oled_height - 1);
    oled_cmd(CMD_SET_DISPLAY_OFFSET);    oled_cmd(0x00);
    oled_cmd(CMD_SET_START_LINE | 0x00);
    oled_cmd(CMD_CHARGE_PUMP);           oled_cmd(0x14);
    oled_cmd(CMD_MEMORY_MODE);           oled_cmd(0x02);
    oled_cmd(CMD_SEG_REMAP | 0x01);
    oled_cmd(CMD_COM_SCAN_DEC);
    oled_cmd(CMD_SET_COM_PINS);          oled_cmd(oled_height == 64 ? 0x12 : 0x02);
    oled_cmd(CMD_SET_CONTRAST);          oled_cmd(0x8F);
    oled_cmd(CMD_SET_PRECHARGE);         oled_cmd(0xF1);
    oled_cmd(CMD_SET_VCOM_DETECT);       oled_cmd(0x40);
    oled_cmd(CMD_DISPLAY_ALL_ON_RESUME);
    oled_cmd(CMD_NORMAL_DISPLAY);
    oled_cmd(CMD_DISPLAY_ON);
    oled_cmd(CMD_DEACTIVATE_SCROLL);

    oled_clear();
    oled_display();
}

/**
 * @brief Turn the display off.
 * @req REQ-ROVARI-SSD1306-0010
 */
void oled_off(void)  { oled_cmd(CMD_DISPLAY_OFF); }
/**
 * @brief Turn the display on.
 * @req REQ-ROVARI-SSD1306-0010
 */
void oled_on(void)   { oled_cmd(CMD_DISPLAY_ON); }

/**
 * @brief Set panel contrast.
 * @param[in] val Contrast 0-255.
 * @req REQ-ROVARI-SSD1306-0010
 */
void oled_contrast(uint8_t val)
{
    oled_cmd(CMD_SET_CONTRAST); oled_cmd(val);
}

/**
 * @brief Set the panel orientation/mirroring.
 * @param[in] orient Orientation mode.
 * @req REQ-ROVARI-SSD1306-0010
 */
void oled_set_orientation(OledOrient orient)
{
    oled_cmd(CMD_DEACTIVATE_SCROLL);
    switch (orient) {
    default:
    case OLED_ORIENT_0:   oled_cmd(CMD_SEG_REMAP|0x01); oled_cmd(CMD_COM_SCAN_DEC); break;
    case OLED_ORIENT_180: oled_cmd(CMD_SEG_REMAP|0x00); oled_cmd(CMD_COM_SCAN_INC); break;
    case OLED_MIRROR_X:   oled_cmd(CMD_SEG_REMAP|0x00); oled_cmd(CMD_COM_SCAN_DEC); break;
    case OLED_MIRROR_Y:   oled_cmd(CMD_SEG_REMAP|0x01); oled_cmd(CMD_COM_SCAN_INC); break;
    }
}

/**
 * @brief Set normal or inverted display.
 * @param[in] inv Non-zero inverts the display.
 * @req REQ-ROVARI-SSD1306-0010
 */
void oled_invert(uint8_t inv)
{
    oled_cmd(inv ? CMD_INVERT_DISPLAY : CMD_NORMAL_DISPLAY);
}

/* Clear / flush */

/**
 * @brief Clear the framebuffer.
 * @req REQ-ROVARI-SSD1306-0011
 */
void oled_clear(void)
{
    memset(screen_buffer, 0, sizeof(screen_buffer));
}

/**
 * @brief Clear one framebuffer page.
 * @param[in] page Page index.
 * @req REQ-ROVARI-SSD1306-0011
 */
void oled_clear_line(uint8_t page)
{
    if (page < oled_pages) {
        SEVS_INVARIANT(page < SSD1306_MAX_PAGES);
        memset(screen_buffer[page], 0, SSD1306_WIDTH);
    }
}

/**
 * @brief Flush the whole framebuffer to the panel.
 * @req REQ-ROVARI-SSD1306-0011
 */
void oled_display(void)
{
    SEVS_INVARIANT(oled_pages <= SSD1306_MAX_PAGES);
    for (uint8_t p = 0; p < oled_pages; p++) {
        oled_set_pos(p, 0);
        oled_data(screen_buffer[p], SSD1306_WIDTH);
    }
}

/**
 * @brief Flush one framebuffer page to the panel.
 * @param[in] page Page index.
 * @req REQ-ROVARI-SSD1306-0011
 */
void oled_update_line(uint8_t page)
{
    if (page < oled_pages) {
        SEVS_INVARIANT(page < SSD1306_MAX_PAGES);
        oled_set_pos(page, 0);
        oled_data(screen_buffer[page], SSD1306_WIDTH);
    }
}

/**
 * @brief Flush part of one framebuffer page to the panel.
 * @param[in] page      Page index.
 * @param[in] start_col Starting column.
 * @param[in] num_cols  Column count.
 * @req REQ-ROVARI-SSD1306-0011
 */
void oled_update_partial(uint8_t page, uint8_t start_col, uint8_t num_cols)
{
    if (page >= oled_pages || start_col >= SSD1306_WIDTH) return;
    SEVS_INVARIANT(page < SSD1306_MAX_PAGES);
    if (start_col + num_cols > SSD1306_WIDTH)
        num_cols = SSD1306_WIDTH - start_col;
    oled_set_pos(page, start_col);
    oled_data(&screen_buffer[page][start_col], num_cols);
}

/* Drawing */

/**
 * @brief Set/clear/invert one framebuffer pixel with clipping.
 * @param[in] x     X coordinate.
 * @param[in] y     Y coordinate.
 * @param[in] color Color/op.
 */
static inline void set_pixel_fast(int16_t x, int16_t y, OledColor color)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= oled_height) return;
    uint8_t page = y / 8;
    uint8_t bit  = y & 7;
    switch (color) {
    case OLED_COLOR_WHITE:  screen_buffer[page][x] |=  (1 << bit); break;
    case OLED_COLOR_BLACK:  screen_buffer[page][x] &= ~(1 << bit); break;
    case OLED_COLOR_INVERT: screen_buffer[page][x] ^=  (1 << bit); break;
    }
}

/**
 * @brief Draw one pixel.
 * @param[in] x     X coordinate.
 * @param[in] y     Y coordinate.
 * @param[in] color Pixel color/op.
 * @req REQ-ROVARI-SSD1306-0012
 */
void oled_pixel(int16_t x, int16_t y, OledColor color) { set_pixel_fast(x, y, color); }

/**
 * @brief Draw a horizontal line.
 * @param[in] x     Start X.
 * @param[in] y     Y coordinate.
 * @param[in] w     Width.
 * @param[in] color Color/op.
 * @req REQ-ROVARI-SSD1306-0012
 */
void oled_hline(int16_t x, int16_t y, int16_t w, OledColor color)
{
    for (int16_t i = 0; i < w; i++) set_pixel_fast(x + i, y, color);
}

/**
 * @brief Draw a vertical line.
 * @param[in] x     X coordinate.
 * @param[in] y     Start Y.
 * @param[in] h     Height.
 * @param[in] color Color/op.
 * @req REQ-ROVARI-SSD1306-0012
 */
void oled_vline(int16_t x, int16_t y, int16_t h, OledColor color)
{
    for (int16_t i = 0; i < h; i++) set_pixel_fast(x, y + i, color);
}

/**
 * @brief Draw a line (Bresenham).
 * @param[in] x0    Start X.
 * @param[in] y0    Start Y.
 * @param[in] x1    End X.
 * @param[in] y1    End Y.
 * @param[in] color Color/op.
 * @req REQ-ROVARI-SSD1306-0012
 */
void oled_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, OledColor color)
{
    int16_t dx = x1 - x0, dy = y1 - y0;
    int16_t sx = (dx > 0) ? 1 : -1, sy = (dy > 0) ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int16_t err = dx - dy;
    /* @sevs-bound: Bresenham; terminates when (x0,y0) reaches (x1,y1). */
    while (1) {
        set_pixel_fast(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/**
 * @brief Draw a rectangle outline.
 * @param[in] x     Top-left X.
 * @param[in] y     Top-left Y.
 * @param[in] w     Width.
 * @param[in] h     Height.
 * @param[in] color Color/op.
 * @req REQ-ROVARI-SSD1306-0012
 */
void oled_rect(int16_t x, int16_t y, int16_t w, int16_t h, OledColor color)
{
    oled_hline(x, y, w, color);
    oled_hline(x, y + h - 1, w, color);
    oled_vline(x, y, h, color);
    oled_vline(x + w - 1, y, h, color);
}

/**
 * @brief Draw a filled rectangle.
 * @param[in] x     Top-left X.
 * @param[in] y     Top-left Y.
 * @param[in] w     Width.
 * @param[in] h     Height.
 * @param[in] color Color/op.
 * @req REQ-ROVARI-SSD1306-0012
 */
void oled_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, OledColor color)
{
    for (int16_t row = 0; row < h; row++) oled_hline(x, y + row, w, color);
}

/**
 * @brief Draw a circle outline (midpoint algorithm).
 * @param[in] cx    Center X.
 * @param[in] cy    Center Y.
 * @param[in] r     Radius.
 * @param[in] color Color/op.
 * @req REQ-ROVARI-SSD1306-0012
 */
void oled_circle(int16_t cx, int16_t cy, int16_t r, OledColor color)
{
    int16_t x = r, y = 0, d = 1 - r;
    /* @sevs-bound: midpoint circle; y rises to x each step, terminates at x<y. */
    while (x >= y) {
        set_pixel_fast(cx+x,cy+y,color); set_pixel_fast(cx-x,cy+y,color);
        set_pixel_fast(cx+x,cy-y,color); set_pixel_fast(cx-x,cy-y,color);
        set_pixel_fast(cx+y,cy+x,color); set_pixel_fast(cx-y,cy+x,color);
        set_pixel_fast(cx+y,cy-x,color); set_pixel_fast(cx-y,cy-x,color);
        y++;
        if (d <= 0) { d += 2*y + 1; } else { x--; d += 2*(y-x) + 1; }
    }
}

/* Text */

/**
 * @brief Render one glyph into the framebuffer.
 * @param[in] ch   Character.
 * @param[in] x    Column (pixels).
 * @param[in] page Page index.
 * @req REQ-ROVARI-SSD1306-0013
 */
void oled_put_char(char ch, uint8_t x, uint8_t page)
{
    if (page >= oled_pages || x + 8 > SSD1306_WIDTH) return;
    SEVS_INVARIANT(page < SSD1306_MAX_PAGES);
    ch = sanitize_char(ch);
    memcpy(&screen_buffer[page][x], &OledFont[ch - 32][0], 8);
}

/**
 * @brief Render a string into the framebuffer.
 * @param[in] s        String to render.
 * @param[in] page     Page index.
 * @param[in] col_char Starting character column.
 * @req REQ-ROVARI-SSD1306-0013
 * @req REQ-ROVARI-SSD1306-0020
 */
void oled_write_string(const char* s, uint8_t page, uint8_t col_char)
{
    SEVS_REQUIRE_NOT_NULL(s);
    uint8_t x = col_char * 8;
    /* @sevs-bound: terminated by NUL or the right screen edge. */
    while (*s && x + 8 <= SSD1306_WIDTH) {
        char ch = sanitize_char(*s++);
        memcpy(&screen_buffer[page][x], &OledFont[ch - 32][0], 8);
        x += 8;
    }
}

/**
 * @brief Render a printf-formatted line with a change cache.
 * @param[in] line Page index.
 * @param[in] fmt  Format string.
 * @req REQ-ROVARI-SSD1306-0013
 * @req REQ-ROVARI-SSD1306-0020
 */
void oled_printf_line(uint8_t line, const char* fmt, ...)
{
    SEVS_REQUIRE_NOT_NULL(fmt);
    char buf[17];
    if (line >= oled_pages) return;
    SEVS_INVARIANT(line < SSD1306_MAX_PAGES);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (strcmp(buf, last_text[line]) == 0) return;
    strcpy(last_text[line], buf);

    memset(screen_buffer[line], 0, SSD1306_WIDTH);
    uint8_t col = 0;
    for (const char* p = buf; *p && (col + 8) <= SSD1306_WIDTH; p++) {
        char ch = sanitize_char(*p);
        memcpy(&screen_buffer[line][col], &OledFont[ch - 32][0], 8);
        col += 8;
    }
    oled_set_pos(line, 0);
    oled_data(screen_buffer[line], SSD1306_WIDTH);
}

/* Icons / bitmaps */

/**
 * @brief Draw a battery icon with a fill level.
 * @param[in] percentage Charge 0-100.
 * @param[in] x          Column.
 * @param[in] page       Page index.
 * @req REQ-ROVARI-SSD1306-0014
 */
void oled_draw_battery(uint8_t percentage, uint8_t x, uint8_t page)
{
    if (percentage > 100) percentage = 100;
    if (x + 8 > SSD1306_WIDTH || page >= oled_pages) return;
    SEVS_INVARIANT(page < SSD1306_MAX_PAGES);

    memcpy(&screen_buffer[page][x], BatteryIcon, 8);
    uint8_t fill_w = (percentage * 6) / 100;
    for (uint8_t i = 1; i <= fill_w && i <= 6; i++)
        screen_buffer[page][x + i] |= 0x7E;
}

/**
 * @brief Blit a bitmap into the framebuffer with clipping.
 * @param[in] data    Bitmap data (page-major).
 * @param[in] x       Column.
 * @param[in] page    Starting page.
 * @param[in] w       Width in columns.
 * @param[in] h_pages Height in pages.
 * @req REQ-ROVARI-SSD1306-0014
 * @req REQ-ROVARI-SSD1306-0020
 */
void oled_draw_bitmap(const uint8_t* data, uint8_t x, uint8_t page,
                       uint8_t w, uint8_t h_pages)
{
    SEVS_REQUIRE_NOT_NULL(data);
    for (uint8_t p = 0; p < h_pages && (page + p) < oled_pages; p++) {
        uint8_t cw = w;
        if (x + cw > SSD1306_WIDTH) cw = SSD1306_WIDTH - x;
        memcpy(&screen_buffer[page + p][x], &data[p * w], cw);
    }
}

/* Info */

/**
 * @brief Get the panel height in pixels.
 * @return Height in pixels.
 * @req REQ-ROVARI-SSD1306-0015
 */
uint8_t  oled_get_height(void) { return oled_height; }
/**
 * @brief Get the panel page count.
 * @return Number of 8-pixel pages.
 * @req REQ-ROVARI-SSD1306-0015
 */
uint8_t  oled_get_pages(void)  { return oled_pages; }
/**
 * @brief Get a pointer to the framebuffer.
 * @return Framebuffer base pointer.
 * @req REQ-ROVARI-SSD1306-0015
 */
uint8_t* oled_get_buffer(void) { return &screen_buffer[0][0]; }
