/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_st7796s.c - ST7796S 480x320 TFT LCD driver (CH32H417 port)
 *
 * IMPORTANT: The CH32H417 has mixed I/O voltage domains. PB10-PB13
 * are on the VIO18 (1.8V) domain and CANNOT drive 3.3V displays.
 * All LCD signals use pins on the VDDIO (3.3V) domain:
 *   SPI1_SCK  = PA5 (AF5)
 *   SPI1_MOSI = PA7 (AF5)
 *   LCD_DC    = PA0
 *   LCD_RST   = PA1
 *   LCD_CS    = PA3
 *
 * SPI is configured as 1-line TX only (write-only, no MISO needed).
 * DMA bulk transfers use dma_spi_tx_*() from rovari_dma.
 */

#include "rovari_st7796s.h"
#include "rovari_dma.h"
#include "rovari_spi.h"
#include "debug.h"
#include <string.h>

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

/* -- Internal SPI helpers ------------------------------------------ */

/*
 * We bypass rovari_spi for single-byte writes because:
 *   1. rovari_spi uses full-duplex transfer (waits for RXNE),
 *      but the LCD is write-only (1-line TX mode).
 *   2. The LCD protocol needs DC pin toggling between command/data
 *      bytes, so we need tight control over CS/DC around each byte.
 *
 * SPI1 is used because it is on the HB2 bus and its pins (PA5/PA7)
 * are on the VDDIO (3.3V) I/O domain. SPI2 pins PB10-PB13 are on
 * the VIO18 (1.8V) domain and cannot drive 3.3V displays.
 */

/* Raw SPI1 init in 1-line TX mode */
static void lcd_spi_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef  spi  = {0};

    /* Enable clocks: AFIO + GPIOA + SPI1 (all on HB2) */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOA |
                          RCC_HB2Periph_SPI1, ENABLE);

    /* PA5 = SPI1_SCK (AF5) */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF5);
    gpio.GPIO_Pin   = GPIO_Pin_5;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOA, &gpio);

    /* PA7 = SPI1_MOSI (AF5) */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF5);
    gpio.GPIO_Pin   = GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOA, &gpio);

    /* SPI1: 1-line TX, master, 8-bit, mode 0 */
    spi.SPI_Direction         = SPI_Direction_1Line_Tx;
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_CPOL              = SPI_CPOL_Low;
    spi.SPI_CPHA              = SPI_CPHA_1Edge;
    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode1;  /* /4 default */
#ifdef LCD_SPI_PRESCALER
    spi.SPI_BaudRatePrescaler = LCD_SPI_PRESCALER;
#endif
    spi.SPI_FirstBit          = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial     = 7;

    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);
}

/* Send one byte over SPI1 (blocking, no DMA) */
static void spi_write_byte(uint8_t data)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
}

/* Send a command byte (DC=low) */
static void lcd_write_cmd(uint8_t cmd)
{
    LCD_CS_LOW();
    LCD_DC_LOW();
    spi_write_byte(cmd);
    LCD_CS_HIGH();
}

/* Send a data byte (DC=high) */
static void lcd_write_data(uint8_t data)
{
    LCD_CS_LOW();
    LCD_DC_HIGH();
    spi_write_byte(data);
    LCD_CS_HIGH();
}

/* Send command + single data byte */
static void lcd_write_reg(uint8_t reg, uint8_t val)
{
    lcd_write_cmd(reg);
    lcd_write_data(val);
}

/* -- Control pin GPIO init ----------------------------------------- */
static void lcd_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_HB2PeriphClockCmd(LCD_DC_RCC | LCD_RST_RCC | LCD_CS_RCC, ENABLE);

    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;

    gpio.GPIO_Pin = LCD_DC_PIN;
    GPIO_Init(LCD_DC_PORT, &gpio);

    gpio.GPIO_Pin = LCD_RST_PIN;
    GPIO_Init(LCD_RST_PORT, &gpio);

    gpio.GPIO_Pin = LCD_CS_PIN;
    GPIO_Init(LCD_CS_PORT, &gpio);
}

/* -- Hardware reset ------------------------------------------------ */
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

void lcd_init(void)
{
    lcd_spi_init();
    dma_spi_tx_init(LCD_SPI_INST);
    lcd_gpio_init();
    lcd_hw_reset();

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

    /* Tearing Effect Line On (V-sync mode) - reduces flicker */
    lcd_write_cmd(0x35);
    lcd_write_data(0x00);  /* V-blank only */

    /* Display On */
    lcd_write_cmd(0x29);
    Delay_Ms(25);

    /* Set rotation to configured default */
    lcd_set_rotation(LCD_ROTATION);
}

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
}

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
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

void lcd_write_data_16(uint16_t data)
{
    LCD_CS_LOW();
    LCD_DC_HIGH();
    spi_write_byte(data >> 8);
    spi_write_byte(data & 0xFF);
    LCD_CS_HIGH();
}

/*
 * Set LCD SPI clock speed at runtime.
 * mode: 0=/2 (75MHz), 1=/4 (37.5MHz), 2=/8 (18.75MHz), etc.
 */
void lcd_set_spi_speed(uint8_t mode)
{
    SPI_Cmd(SPI1, DISABLE);
    SPI1->CTLR1 = (SPI1->CTLR1 & ~((uint16_t)0x0038)) |
                   ((uint16_t)(mode & 0x07) << 3);
    SPI_Cmd(SPI1, ENABLE);
}

void lcd_clear(uint16_t color)
{
    uint32_t total = (uint32_t)_lcd_w * _lcd_h;
    lcd_set_window(0, 0, _lcd_w - 1, _lcd_h - 1);
    LCD_CS_LOW();
    LCD_DC_HIGH();
    for (uint32_t i = 0; i < total; i++) {
        spi_write_byte(color >> 8);
        spi_write_byte(color & 0xFF);
    }
    LCD_CS_HIGH();
}

void lcd_fill_rect(uint16_t x, uint16_t y,
                   uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    LCD_CS_LOW();
    LCD_DC_HIGH();
    uint32_t total = (uint32_t)w * h;
    for (uint32_t i = 0; i < total; i++) {
        spi_write_byte(color >> 8);
        spi_write_byte(color & 0xFF);
    }
    LCD_CS_HIGH();
}

void lcd_push_pixels(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h,
                     uint16_t* pixels)
{
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
    while (total_bytes > 0) {
        uint16_t chunk = (total_bytes > 65534) ? 65534 : (uint16_t)total_bytes;
        dma_spi_tx_start(LCD_SPI_INST, buf, chunk);
        dma_spi_tx_wait(LCD_SPI_INST);
        buf += chunk;
        total_bytes -= chunk;
    }

    LCD_CS_HIGH();
}

void lcd_push_pixels_raw(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         uint16_t* pixels)
{
    uint32_t total_bytes = (uint32_t)w * h * 2;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    LCD_CS_LOW();
    LCD_DC_HIGH();

    uint8_t* buf = (uint8_t*)pixels;
    while (total_bytes > 0) {
        uint16_t chunk = (total_bytes > 65534) ? 65534 : (uint16_t)total_bytes;
        dma_spi_tx_start(LCD_SPI_INST, buf, chunk);
        dma_spi_tx_wait(LCD_SPI_INST);
        buf += chunk;
        total_bytes -= chunk;
    }

    LCD_CS_HIGH();
}

/* -- Non-blocking flush for double-buffered rendering / LVGL -------- */

/*
 * Double-buffer workflow:
 *
 *   1. CPU renders into buffer A
 *   2. lcd_flush_start(region, buffer_a)   -- starts DMA, returns immediately
 *   3. CPU renders into buffer B while DMA sends A
 *   4. lcd_flush_wait()                    -- blocks until DMA done
 *   5. lcd_flush_start(region, buffer_b)   -- starts DMA on B
 *   6. CPU renders into buffer A            -- repeat
 *
 * For LVGL, the flush callback calls lcd_flush_start(), and
 * lv_display_flush_ready() is called after lcd_flush_wait().
 *
 * The byte-swap is done in-place before DMA starts. Since LVGL
 * uses two separate draw buffers, the swap doesn't corrupt the
 * other buffer.
 */

static volatile uint8_t  _flush_active = 0;
static uint8_t*          _flush_ptr    = 0;
static uint32_t          _flush_remaining = 0;

void lcd_flush_start(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h,
                     uint16_t* pixels)
{
    /* Wait for any previous flush to complete */
    lcd_flush_wait();

    uint32_t total_pixels = (uint32_t)w * h;
    uint32_t total_bytes  = total_pixels * 2;

    /* Byte-swap in place */
    for (uint32_t i = 0; i < total_pixels; i++) {
        uint16_t p = pixels[i];
        pixels[i] = (p >> 8) | (p << 8);
    }

    /* Set window and enter data mode */
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    LCD_CS_LOW();
    LCD_DC_HIGH();

    /* Start first DMA chunk */
    _flush_ptr = (uint8_t*)pixels;
    _flush_remaining = total_bytes;
    _flush_active = 1;

    uint16_t chunk = (_flush_remaining > 65534) ? 65534 : (uint16_t)_flush_remaining;
    dma_spi_tx_start(LCD_SPI_INST, _flush_ptr, chunk);
    _flush_ptr += chunk;
    _flush_remaining -= chunk;
}

void lcd_flush_start_raw(uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         uint16_t* pixels)
{
    lcd_flush_wait();

    uint32_t total_bytes = (uint32_t)w * h * 2;

    lcd_set_window(x, y, x + w - 1, y + h - 1);
    LCD_CS_LOW();
    LCD_DC_HIGH();

    /* Use interrupt-driven chained DMA for zero-gap between chunks */
    _flush_active = 2;  /* special flag: chained mode */
    dma_spi_tx_start_chained(LCD_SPI_INST, (const uint8_t*)pixels, total_bytes);
}

void lcd_flush_wait(void)
{
    if (!_flush_active) return;

    if (_flush_active == 2) {
        /* Chained DMA mode - wait for ISR to finish all chunks */
        dma_spi_tx_chain_wait();
        LCD_CS_HIGH();
        _flush_active = 0;
        return;
    }

    /* Original chunked mode */
    dma_spi_tx_wait(LCD_SPI_INST);

    while (_flush_remaining > 0) {
        uint16_t chunk = (_flush_remaining > 65534) ? 65534 : (uint16_t)_flush_remaining;
        dma_spi_tx_start(LCD_SPI_INST, _flush_ptr, chunk);
        dma_spi_tx_wait(LCD_SPI_INST);
        _flush_ptr += chunk;
        _flush_remaining -= chunk;
    }

    LCD_CS_HIGH();
    _flush_active = 0;
}

uint8_t lcd_flush_busy(void)
{
    if (!_flush_active) return 0;

    /* Check if current DMA chunk is done */
    if (dma_spi_tx_busy(LCD_SPI_INST)) return 1;

    /* Current chunk done - start next if any */
    dma_spi_tx_wait(LCD_SPI_INST);  /* clear flags */

    if (_flush_remaining > 0) {
        uint16_t chunk = (_flush_remaining > 65534) ? 65534 : (uint16_t)_flush_remaining;
        dma_spi_tx_start(LCD_SPI_INST, _flush_ptr, chunk);
        _flush_ptr += chunk;
        _flush_remaining -= chunk;
        return 1;
    }

    LCD_CS_HIGH();
    _flush_active = 0;
    return 0;
}

void lcd_backlight(uint8_t on)
{
}

uint16_t lcd_width(void)  { return _lcd_w; }
uint16_t lcd_height(void) { return _lcd_h; }
