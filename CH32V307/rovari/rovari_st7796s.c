/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_st7796s.c
 * @brief ST7796S 480x320 RGB565 TFT driver over SPI2 + DMA (SEVS-Core).
 *
 * Write-only 1-line SPI2 at 36 MHz; DMA bulk transfers via dma_spi_tx_*().
 * Init sequence verified against TFT_eSPI and esp_lcd_st7796 on RV-Boy.
 * SPI polling is bounded so a stalled bus cannot hang the CPU. Integer-only.
 *
 * @copyright (c) 2025 Armstrong Subero
 */

/* System includes */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* SEVS runtime */
#include "sevs_runtime.h"

/* Project includes (WCH HAL) */
#include "debug.h"

/* Local includes */
#include "rovari_st7796s.h"
#include "rovari_dma.h"
#include "rovari_spi.h"
#include "rovari_touch.h"

/* Bounded poll cap for SPI TXE/BSY waits. */
#define LCD_SPI_TIMEOUT  1000000U
#define LCD_DMA_CHUNK_MAX 65534U

/* -- Current dimensions (change with rotation) --------------------- */
static uint16_t _lcd_w;
static uint16_t _lcd_h;

/* -- GPIO fast toggle macros --------------------------------------- */
#define LCD_CS_LOW()    (LCD_CS_PORT->BCR  = LCD_CS_PIN)
#define LCD_CS_HIGH()   (LCD_CS_PORT->BSHR = LCD_CS_PIN)
#define LCD_DC_LOW()    (LCD_DC_PORT->BCR  = LCD_DC_PIN)
#define LCD_DC_HIGH()   (LCD_DC_PORT->BSHR = LCD_DC_PIN)
#define LCD_RST_LOW()   (LCD_RST_PORT->BCR = LCD_RST_PIN)
#define LCD_RST_HIGH()  (LCD_RST_PORT->BSHR= LCD_RST_PIN)
#define LCD_LED_ON()    (LCD_LED_PORT->BSHR= LCD_LED_PIN)
#define LCD_LED_OFF()   (LCD_LED_PORT->BCR = LCD_LED_PIN)

/* -- Internal SPI helpers ------------------------------------------ */

/*
 * We bypass rovari_spi for single-byte writes because:
 *   1. rovari_spi uses full-duplex transfer (waits for RXNE),
 *      but the LCD is write-only (1-line TX mode).
 *   2. The LCD protocol needs DC pin toggling between command/data
 *      bytes, so we need tight control over CS/DC around each byte.
 *
 * For DMA bulk transfers we use dma_spi_tx_start/wait which
 * operates at the DMA channel level and doesn't care about
 * full-duplex vs 1-line.
 *
 * The SPI2 peripheral is initialized here in 1-line TX mode
 * (SPI_Direction_1Line_Tx) instead of using spi_init() which
 * sets full-duplex. This gives us maximum throughput for the
 * display since we never read from it over SPI.
 */

/* Raw SPI2 init in 1-line TX mode at max speed */
/**
 * @brief Initialize SPI2 in 1-line TX mode at max speed.
 */
static void lcd_spi_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef  spi  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    /* PB13 = SPI2_SCK */
    gpio.GPIO_Pin   = GPIO_Pin_13;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    /* PB14 = SPI2_MISO (configure but unused - 1-line TX) */
    gpio.GPIO_Pin  = GPIO_Pin_14;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &gpio);

    /* PB15 = SPI2_MOSI */
    gpio.GPIO_Pin   = GPIO_Pin_15;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    /* SPI2: 1-line TX, master, 8-bit, mode 0, max speed */
    spi.SPI_Direction         = SPI_Direction_1Line_Tx;
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_CPOL              = SPI_CPOL_Low;
    spi.SPI_CPHA              = SPI_CPHA_1Edge;
    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;  /* APB1/2 = 36 MHz */
    spi.SPI_FirstBit          = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial     = 7;

    SPI_Init(SPI2, &spi);
    SPI_Cmd(SPI2, ENABLE);
}

/* Send one byte over SPI2 (blocking, no DMA) */
/**
 * @brief Send one byte over SPI2 (blocking, bounded waits).
 * @param[in] data Byte to send.
 * @req REQ-ROVARI-ST7796S-0020
 */
static void spi2_write_byte(uint8_t data)
{
    uint8_t txe_ok = 0;
    for (uint32_t i = 0U; i < LCD_SPI_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) != RESET) {
            txe_ok = 1;
            break;
        }
    }
    if (!txe_ok) {
        return;
    }
    SPI_I2S_SendData(SPI2, data);
    for (uint32_t i = 0U; i < LCD_SPI_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) != SET) {
            break;
        }
    }
}

/* Send a command byte (DC=low) */
/**
 * @brief Send a command byte (DC low).
 * @param[in] cmd Command byte.
 */
static void lcd_write_cmd(uint8_t cmd)
{
    LCD_CS_LOW();
    LCD_DC_LOW();
    spi2_write_byte(cmd);
    LCD_CS_HIGH();
}

/* Send a data byte (DC=high) */
/**
 * @brief Send a data byte (DC high).
 * @param[in] data Data byte.
 */
static void lcd_write_data(uint8_t data)
{
    LCD_CS_LOW();
    LCD_DC_HIGH();
    spi2_write_byte(data);
    LCD_CS_HIGH();
}

/* Send command + single data byte */
/**
 * @brief Send a command followed by a single data byte.
 * @param[in] reg Command byte.
 * @param[in] val Data byte.
 */
static void lcd_write_reg(uint8_t reg, uint8_t val)
{
    lcd_write_cmd(reg);
    lcd_write_data(val);
}

/* -- Control pin GPIO init ----------------------------------------- */
/**
 * @brief Initialize the LCD control GPIO lines.
 */
static void lcd_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(LCD_LED_RCC | LCD_DC_RCC |
                           LCD_RST_RCC | LCD_CS_RCC, ENABLE);

    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;

    gpio.GPIO_Pin = LCD_LED_PIN;
    GPIO_Init(LCD_LED_PORT, &gpio);

    gpio.GPIO_Pin = LCD_DC_PIN;
    GPIO_Init(LCD_DC_PORT, &gpio);

    gpio.GPIO_Pin = LCD_RST_PIN;
    GPIO_Init(LCD_RST_PORT, &gpio);

    gpio.GPIO_Pin = LCD_CS_PIN;
    GPIO_Init(LCD_CS_PORT, &gpio);
}

/* -- Hardware reset ------------------------------------------------ */
/**
 * @brief Pulse the panel hardware reset line.
 */
static void lcd_hw_reset(void)
{
    LCD_RST_HIGH();
    Delay_Ms(10);
    LCD_RST_LOW();
    Delay_Ms(120);
    LCD_RST_HIGH();
    Delay_Ms(120);
}

/* ===================================================================
 *  Public API
 * =================================================================== */

/**
 * @brief Initialize the ST7796S panel and apply the default rotation.
 * @req REQ-ROVARI-ST7796S-0010
 */
void lcd_init(void)
{
    lcd_spi_init();
    dma_spi_tx_init(LCD_SPI_INST);
    lcd_gpio_init();
    lcd_hw_reset();
    LCD_LED_ON();

    /* --- ST7796S init commands --- */

    /* Command Set Control - enable command set 2 */
    lcd_write_cmd(0xF0);
    lcd_write_data(0xC3);
    lcd_write_cmd(0xF0);
    lcd_write_data(0x96);

    /* Memory Access Control (set properly by lcd_set_rotation) */
    lcd_write_cmd(0x36);
    lcd_write_data(0x48);

    /* Interface Pixel Format - 16-bit RGB565 */
    lcd_write_cmd(0x3A);
    lcd_write_data(0x55);

    /* Display Inversion Control - 1-dot inversion */
    lcd_write_cmd(0xB4);
    lcd_write_data(0x01);

    /* Blanking Porch Control */
    lcd_write_cmd(0xB7);
    lcd_write_data(0xC6);

    /* Display Function Control */
    lcd_write_cmd(0xB6);
    lcd_write_data(0x80);
    lcd_write_data(0x02);
    lcd_write_data(0x3B);

    /* Power Control 1 - VREG1OUT */
    lcd_write_cmd(0xC0);
    lcd_write_data(0x80);
    lcd_write_data(0x65);

    /* Power Control 2 */
    lcd_write_cmd(0xC1);
    lcd_write_data(0x13);

    /* VCOM Control 1 */
    lcd_write_cmd(0xC5);
    lcd_write_data(0x24);

    /* Display Output Control Adjust */
    lcd_write_cmd(0xE8);
    lcd_write_data(0x40);
    lcd_write_data(0x8A);
    lcd_write_data(0x00);
    lcd_write_data(0x00);
    lcd_write_data(0x29);
    lcd_write_data(0x19);
    lcd_write_data(0xA5);
    lcd_write_data(0x33);

    /* Positive Gamma Control */
    lcd_write_cmd(0xE0);
    lcd_write_data(0xF0);
    lcd_write_data(0x09);
    lcd_write_data(0x13);
    lcd_write_data(0x12);
    lcd_write_data(0x12);
    lcd_write_data(0x2B);
    lcd_write_data(0x3C);
    lcd_write_data(0x44);
    lcd_write_data(0x4B);
    lcd_write_data(0x1B);
    lcd_write_data(0x18);
    lcd_write_data(0x17);
    lcd_write_data(0x1D);
    lcd_write_data(0x21);

    /* Negative Gamma Control */
    lcd_write_cmd(0xE1);
    lcd_write_data(0xF0);
    lcd_write_data(0x09);
    lcd_write_data(0x13);
    lcd_write_data(0x0C);
    lcd_write_data(0x0D);
    lcd_write_data(0x27);
    lcd_write_data(0x3B);
    lcd_write_data(0x44);
    lcd_write_data(0x4D);
    lcd_write_data(0x0B);
    lcd_write_data(0x17);
    lcd_write_data(0x17);
    lcd_write_data(0x1D);
    lcd_write_data(0x21);

    /* Disable command set 2 */
    lcd_write_cmd(0xF0);
    lcd_write_data(0x3C);
    lcd_write_cmd(0xF0);
    lcd_write_data(0x69);

    /* Sleep Out */
    lcd_write_cmd(0x11);
    Delay_Ms(120);

    /* Display On */
    lcd_write_cmd(0x29);
    Delay_Ms(25);

    /* Set rotation to configured default */
    lcd_set_rotation(LCD_ROTATION);
    SEVS_INVARIANT(_lcd_w > 0U && _lcd_h > 0U);
}

/**
 * @brief Set display rotation and keep touch rotation in sync.
 * @param[in] rotation Rotation 0-3.
 * @req REQ-ROVARI-ST7796S-0011
 */
void lcd_set_rotation(uint8_t rotation)
{
    /*
     * MADCTL register 0x36 bits:
     *   bit 7: MY  (row address order)
     *   bit 6: MX  (column address order)
     *   bit 5: MV  (row/column exchange)
     *   bit 3: BGR (RGB/BGR order) - ST7796 is BGR
     */
    switch (rotation) {
        case 0:  /* Portrait 320x480 */
            _lcd_w = LCD_PANEL_W;
            _lcd_h = LCD_PANEL_H;
            lcd_write_reg(0x36, 0x48);   /* MY=0, MX=1, MV=0, BGR=1 */
            break;
        case 1:  /* Landscape 480x320 */
            _lcd_w = 480;
            _lcd_h = 320;
            lcd_write_reg(0x36, 0x28);   /* MY=0, MX=0, MV=1, BGR=1 */
            break;
        case 2:  /* Portrait flipped */
            _lcd_w = LCD_PANEL_W;
            _lcd_h = LCD_PANEL_H;
            lcd_write_reg(0x36, 0x88);   /* MY=1, MX=0, MV=0, BGR=1 */
            break;
        case 3:  /* Landscape flipped */
            _lcd_w = 480;
            _lcd_h = 320;
            lcd_write_reg(0x36, 0xE8);   /* MY=1, MX=1, MV=1, BGR=1 */
            break;
        default:
            break;
    }

    SEVS_INVARIANT(_lcd_w > 0U && _lcd_h > 0U);
    /* Keep touch mapping in sync with display rotation */
    touch_set_rotation(rotation);
}

/**
 * @brief Set the active drawing window and begin memory write.
 * @param[in] x0 Left column.
 * @param[in] y0 Top row.
 * @param[in] x1 Right column.
 * @param[in] y1 Bottom row.
 * @req REQ-ROVARI-ST7796S-0012
 */
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    SEVS_INVARIANT(x0 <= x1);
    SEVS_INVARIANT(y0 <= y1);
    lcd_write_cmd(0x2A);        /* Column Address Set */
    lcd_write_data(x0 >> 8);
    lcd_write_data(x0 & 0xFF);
    lcd_write_data(x1 >> 8);
    lcd_write_data(x1 & 0xFF);

    lcd_write_cmd(0x2B);        /* Row Address Set */
    lcd_write_data(y0 >> 8);
    lcd_write_data(y0 & 0xFF);
    lcd_write_data(y1 >> 8);
    lcd_write_data(y1 & 0xFF);

    lcd_write_cmd(0x2C);        /* Memory Write */
}

/**
 * @brief Write one 16-bit pixel (big-endian) to the panel.
 * @param[in] data RGB565 pixel.
 * @req REQ-ROVARI-ST7796S-0013
 */
void lcd_write_data_16(uint16_t data)
{
    LCD_CS_LOW();
    LCD_DC_HIGH();
    spi2_write_byte(data >> 8);
    spi2_write_byte(data & 0xFF);
    LCD_CS_HIGH();
}

/**
 * @brief Fill the entire screen with a color.
 * @param[in] color RGB565 fill color.
 * @req REQ-ROVARI-ST7796S-0013
 */
void lcd_clear(uint16_t color)
{
    SEVS_INVARIANT(_lcd_w > 0U && _lcd_h > 0U);
    uint32_t total = (uint32_t)_lcd_w * _lcd_h;
    lcd_set_window(0, 0, _lcd_w - 1, _lcd_h - 1);
    LCD_CS_LOW();
    LCD_DC_HIGH();
    for (uint32_t i = 0; i < total; i++) {
        spi2_write_byte(color >> 8);
        spi2_write_byte(color & 0xFF);
    }
    LCD_CS_HIGH();
}

/**
 * @brief Fill a rectangle with a color.
 * @param[in] x     Left column.
 * @param[in] y     Top row.
 * @param[in] w     Width.
 * @param[in] h     Height.
 * @param[in] color RGB565 fill color.
 * @req REQ-ROVARI-ST7796S-0013
 */
void lcd_fill_rect(uint16_t x, uint16_t y,
                   uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;
    SEVS_INVARIANT(w > 0U && h > 0U);
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    LCD_CS_LOW();
    LCD_DC_HIGH();
    uint32_t total = (uint32_t)w * h;
    for (uint32_t i = 0; i < total; i++) {
        spi2_write_byte(color >> 8);
        spi2_write_byte(color & 0xFF);
    }
    LCD_CS_HIGH();
}

/**
 * @brief Byte-swap and DMA a pixel buffer to a window.
 * @param[in]     x      Left column.
 * @param[in]     y      Top row.
 * @param[in]     w      Width.
 * @param[in]     h      Height.
 * @param[in,out] pixels RGB565 buffer (byte-swapped in place).
 * @req REQ-ROVARI-ST7796S-0014
 * @req REQ-ROVARI-ST7796S-0021
 * @req REQ-ROVARI-ST7796S-WORKAROUND-001
 */
void lcd_push_pixels(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h,
                     uint16_t* pixels)
{
    SEVS_REQUIRE_NOT_NULL(pixels);
    SEVS_INVARIANT(w > 0U && h > 0U);
    uint32_t total_pixels = (uint32_t)w * h;
    uint32_t total_bytes  = total_pixels * 2;

    /* Byte-swap in place: RISC-V is little-endian, LCD wants big-endian */
    for (uint32_t i = 0; i < total_pixels; i++) {
        uint16_t p = pixels[i];
        pixels[i] = (p >> 8) | (p << 8);
    }

    /* Set the LCD window */
    lcd_set_window(x, y, x + w - 1, y + h - 1);

    /* Enter data mode */
    LCD_CS_LOW();
    LCD_DC_HIGH();

    /* Push via DMA in chunks (DMA counter is 16-bit, max 65535) */
    uint8_t* buf = (uint8_t*)pixels;
    /* @sevs-bound: total_bytes strictly decreases by chunk each iteration. */
    while (total_bytes > 0) {
        uint16_t chunk = (total_bytes > 65534) ? 65534 : (uint16_t)total_bytes;
        dma_spi_tx_start(LCD_SPI_INST, buf, chunk);
        dma_spi_tx_wait(LCD_SPI_INST);
        buf += chunk;
        total_bytes -= chunk;
    }

    LCD_CS_HIGH();
}

/**
 * @brief Turn the backlight on or off.
 * @param[in] on Non-zero turns the backlight on.
 * @req REQ-ROVARI-ST7796S-0015
 */
void lcd_backlight(uint8_t on)
{
    if (on) LCD_LED_ON();
    else    LCD_LED_OFF();
}

/**
 * @brief Get current display width.
 * @return Width in pixels.
 * @req REQ-ROVARI-ST7796S-0015
 */
uint16_t lcd_width(void)  { return _lcd_w; }
/**
 * @brief Get current display height.
 * @return Height in pixels.
 * @req REQ-ROVARI-ST7796S-0015
 */
uint16_t lcd_height(void) { return _lcd_h; }
