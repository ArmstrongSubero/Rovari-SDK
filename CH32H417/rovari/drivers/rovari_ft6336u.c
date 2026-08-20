/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_ft6336u.c - FT6336U capacitive touch controller driver
 *
 * Uses the rovari_swi2c software I2C library for communication.
 * This avoids conflicts with the hardware I2C peripheral and allows
 * pin assignment on any VDDIO (3.3V) domain pins.
 *
 * Optionally, define TOUCH_USE_HW_I2C before including this file
 * to use hardware I2C instead (requires rovari_i2c and a free I2C
 * instance on 3.3V domain pins).
 *
 * Reads both touch points in a single I2C burst (11 bytes from 0x02).
 * Proven working on RV-Boy and CH32H417 eval board hardware.
 */

#include "rovari_ft6336u.h"
#include "debug.h"
#include <string.h>

/* -- FT6336U registers --------------------------------------------- */
#define FT_REG_TD_STATUS    0x02
#define FT_REG_CHIP_ID      0xA3

/* -- Touch rotation state ------------------------------------------ */
static uint8_t _touch_rotation = 1;  /* default: landscape, matching LCD_ROTATION */

/* -- I2C transport ------------------------------------------------- */

#ifdef TOUCH_USE_HW_I2C

/* Hardware I2C path */
#include "rovari_i2c.h"
#include "rovari_ft6336u.h"

static bool ft_read_regs(uint8_t reg, uint8_t* buf, int len)
{
    return (i2c_read_buf(TOUCH_I2C_INSTANCE, FT6336U_ADDR, reg, buf, len) == 0);
}

static bool ft_write_reg(uint8_t reg, uint8_t val)
{
    return (i2c_write_reg(TOUCH_I2C_INSTANCE, FT6336U_ADDR, reg, val) == 0);
}

#else

/* Software I2C path (default) */
#include "rovari_swi2c.h"

static SoftI2c _touch_i2c;

static bool ft_read_regs(uint8_t reg, uint8_t* buf, int len)
{
    return swi2c_read_reg(&_touch_i2c, FT6336U_ADDR, reg, buf, (uint16_t)len);
}

static bool ft_write_reg(uint8_t reg, uint8_t val)
{
    return swi2c_write_reg(&_touch_i2c, FT6336U_ADDR, reg, &val, 1);
}

#endif /* TOUCH_USE_HW_I2C */

/* -- GPIO init ----------------------------------------------------- */
static void touch_gpio_init(void)
{
    GPIO_InitTypeDef g = {0};

    /* Enable GPIO clocks for INT and RST pins */
    RCC_HB2PeriphClockCmd(TOUCH_INT_RCC | TOUCH_RST_RCC, ENABLE);

#ifndef TOUCH_USE_HW_I2C
    /* Software I2C: initialize the bit-bang bus */
    swi2c_init(&_touch_i2c,
               TOUCH_SCL_PORT, TOUCH_SCL_PIN,
               TOUCH_SDA_PORT, TOUCH_SDA_PIN);
#else
    /* Hardware I2C: i2c_init() handles SCL/SDA pin config */
    i2c_init(TOUCH_I2C_INSTANCE, 400000);
#endif

    /* INT - input pull-up (active low interrupt from FT6336U) */
    g.GPIO_Pin  = TOUCH_INT_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(TOUCH_INT_PORT, &g);

    /* RST - push-pull output */
    g.GPIO_Pin   = TOUCH_RST_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(TOUCH_RST_PORT, &g);
}

static void touch_hw_reset(void)
{
    GPIO_WriteBit(TOUCH_RST_PORT, TOUCH_RST_PIN, Bit_SET);   Delay_Ms(10);
    GPIO_WriteBit(TOUCH_RST_PORT, TOUCH_RST_PIN, Bit_RESET); Delay_Ms(50);
    GPIO_WriteBit(TOUCH_RST_PORT, TOUCH_RST_PIN, Bit_SET);   Delay_Ms(200);
}

/* =================================================================
 *  Public API
 * ================================================================= */

bool touch_init(void)
{
    touch_gpio_init();
    touch_hw_reset();

    uint8_t chip_id = touch_read_chip_id();
    printf("Touch chip ID: 0x%02X", chip_id);
    if (chip_id == 0x64 || chip_id == 0x00)
        printf(" (FT6336U OK)\r\n");
    else
        printf(" (unexpected, may still work)\r\n");

    /* Keep the FT6336U awake - disable auto-sleep/hibernate.
     * Without this, the chip enters standby after a few seconds
     * of no touch activity and stops responding to I2C reads.
     * Reg 0xA5 = power mode: 0x00 = active (no sleep)
     * Reg 0x86 = interrupt mode: 0x00 = polling (continuous) */
    uint8_t pwr = 0x00;
    ft_write_reg(0xA5, pwr);
    ft_write_reg(0x86, 0x00);

    return true;
}

uint8_t touch_read_chip_id(void)
{
    uint8_t id = 0;
    ft_read_regs(FT_REG_CHIP_ID, &id, 1);
    return id;
}

/*
 * FT6336U register layout (0x02..0x0C = 11 bytes):
 *   [0]  0x02  TD_STATUS  (touch count in bits 3:0)
 *   [1]  0x03  P1_XH      (event[7:6], X_high[3:0])
 *   [2]  0x04  P1_XL
 *   [3]  0x05  P1_YH      (ID[7:4], Y_high[3:0])
 *   [4]  0x06  P1_YL
 *   [5]  0x07  P1_WEIGHT
 *   [6]  0x08  P1_MISC
 *   [7]  0x09  P2_XH      (event[7:6], X_high[3:0])
 *   [8]  0x0A  P2_XL
 *   [9]  0x0B  P2_YH      (ID[7:4], Y_high[3:0])
 *   [10] 0x0C  P2_YL
 */
void touch_read(TouchData* data)
{
    uint8_t buf[11];

    memset(data, 0, sizeof(TouchData));

    if (!ft_read_regs(FT_REG_TD_STATUS, buf, 11))
        return;

    uint8_t touches = buf[0] & 0x0F;

    if (touches == 0 || touches > 2) {
        data->touched     = false;
        data->num_touches = 0;
        return;
    }

    data->touched     = true;
    data->num_touches = touches;

    /* Extract slot A (registers 0x03-0x06 = buf[1..4]) */
    uint16_t ax = ((buf[1] & 0x0F) << 8) | buf[2];
    uint16_t ay = ((buf[3] & 0x0F) << 8) | buf[4];
    uint8_t  a_id = (buf[3] >> 4) & 0x0F;

    if (touches == 1) {
        data->x   = ax;
        data->y   = ay;
        data->id  = a_id;
        return;
    }

    /* Extract slot B (registers 0x09-0x0C = buf[7..10]) */
    uint16_t bx = ((buf[7] & 0x0F) << 8) | buf[8];
    uint16_t by = ((buf[9] & 0x0F) << 8) | buf[10];
    uint8_t  b_id = (buf[9] >> 4) & 0x0F;

    /* Sort by ID: P1 gets lower ID, P2 gets higher ID */
    if (a_id <= b_id) {
        data->x   = ax;  data->y   = ay;  data->id  = a_id;
        data->x2  = bx;  data->y2  = by;  data->id2 = b_id;
    } else {
        data->x   = bx;  data->y   = by;  data->id  = b_id;
        data->x2  = ax;  data->y2  = ay;  data->id2 = a_id;
    }
}

void touch_set_rotation(uint8_t rotation)
{
    _touch_rotation = rotation & 0x03;
}

/*
 * Map raw panel coordinates to screen coordinates.
 *
 * The FT6336U always reports in the panel's native portrait frame
 * (raw_x: 0-319, raw_y: 0-479) regardless of MADCTL rotation.
 *
 *   Rotation 0 (portrait 320x480):   sx = rx,          sy = ry
 *   Rotation 1 (landscape 480x320):  sx = ry,          sy = 319 - rx
 *   Rotation 2 (portrait flip):      sx = 319 - rx,    sy = 479 - ry
 *   Rotation 3 (landscape flip):     sx = 479 - ry,    sy = rx
 */
static void remap_touch(uint16_t rx, uint16_t ry,
                        uint16_t* sx, uint16_t* sy)
{
    switch (_touch_rotation) {
        case 0:
            *sx = rx;
            *sy = ry;
            break;
        case 1:
            *sx = ry;
            *sy = (rx <= 319) ? (319 - rx) : 0;
            break;
        case 2:
            *sx = (rx <= 319) ? (319 - rx) : 0;
            *sy = (ry <= 479) ? (479 - ry) : 0;
            break;
        case 3:
            *sx = (ry <= 479) ? (479 - ry) : 0;
            *sy = rx;
            break;
        default:
            *sx = rx;
            *sy = ry;
            break;
    }
}

void touch_read_lcd(TouchData* data)
{
    touch_read(data);

    if (!data->touched)
        return;

    uint16_t rx = data->x;
    uint16_t ry = data->y;
    remap_touch(rx, ry, &data->x, &data->y);

    if (data->num_touches >= 2) {
        rx = data->x2;
        ry = data->y2;
        remap_touch(rx, ry, &data->x2, &data->y2);
    }
}
