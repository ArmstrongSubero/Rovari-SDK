/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_ft6336u.c
 * @brief FT6336U capacitive touch controller driver.
 *
 * Uses bit-bang I2C to avoid contention with the hardware I2C peripheral.
 * Reads both touch points in a single 11-byte burst from 0x02. Provides
 * the strong touch_set_rotation that overrides the weak stub in
 * rovari_touch.c. Verified on RV-Boy hardware.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_ft6336u.h"

/* FT6336U registers */
#define FT_REG_TD_STATUS    0x02
#define FT_REG_CHIP_ID      0xA3
#define FT_TOUCH_BURST_LEN  11
#define TOUCH_X_MAX         319U
#define TOUCH_Y_MAX         479U

/* Touch rotation state */
static uint8_t s_touch_rotation = 1;  /* default: landscape */

/* Software I2C bit-bang */

/**
 * @brief Short bit-bang I2C timing delay.
 */
static void i2c_delay(void)
{
    volatile int i = 60;
    /* @sevs-bound: fixed 60-iteration timing delay. */
    while (i--) {
        /* spin */
    }
}

#define SDA_HIGH()  (TOUCH_SDA_PORT->BSHR = TOUCH_SDA_PIN)
#define SDA_LOW()   (TOUCH_SDA_PORT->BCR  = TOUCH_SDA_PIN)
#define SCL_HIGH()  (TOUCH_SCL_PORT->BSHR = TOUCH_SCL_PIN)
#define SCL_LOW()   (TOUCH_SCL_PORT->BCR  = TOUCH_SCL_PIN)
#define SDA_READ()  (GPIO_ReadInputDataBit(TOUCH_SDA_PORT, TOUCH_SDA_PIN))

/**
 * @brief Switch the SDA line to input (pull-up) for reads.
 */
static void sda_input(void)
{
    GPIO_InitTypeDef g = {0};
    g.GPIO_Pin   = TOUCH_SDA_PIN;
    g.GPIO_Mode  = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TOUCH_SDA_PORT, &g);
}

/**
 * @brief Switch the SDA line to open-drain output for writes.
 */
static void sda_output(void)
{
    GPIO_InitTypeDef g = {0};
    g.GPIO_Pin   = TOUCH_SDA_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TOUCH_SDA_PORT, &g);
}

/**
 * @brief Emit an I2C start condition.
 */
static void i2c_start(void)
{
    sda_output();
    SDA_HIGH(); SCL_HIGH(); i2c_delay();
    SDA_LOW(); i2c_delay();
    SCL_LOW(); i2c_delay();
}

/**
 * @brief Emit an I2C stop condition.
 */
static void i2c_stop(void)
{
    sda_output();
    SCL_LOW(); SDA_LOW(); i2c_delay();
    SCL_HIGH(); i2c_delay();
    SDA_HIGH(); i2c_delay();
}

/**
 * @brief Write one byte and return the ACK bit.
 * @return 0 if the slave ACKed, non-zero on NACK.
 */
static uint8_t i2c_write_byte(uint8_t data)
{
    uint8_t ack;
    sda_output();
    for (int i = 7; i >= 0; i--) {
        if (data & (1 << i)) { SDA_HIGH(); } else { SDA_LOW(); }
        i2c_delay(); SCL_HIGH(); i2c_delay(); SCL_LOW(); i2c_delay();
    }
    sda_input();
    SCL_HIGH(); i2c_delay();
    ack = SDA_READ();
    SCL_LOW(); i2c_delay();
    sda_output();
    SEVS_INVARIANT(ack == 0U || ack == 1U);
    return ack;
}

/**
 * @brief Read one byte, sending ACK or NACK afterward.
 * @param[in] nack Non-zero to NACK (last byte), zero to ACK.
 * @return The byte read.
 */
static uint8_t i2c_read_byte(uint8_t nack)
{
    uint8_t data = 0;
    sda_input();
    for (int i = 7; i >= 0; i--) {
        SCL_HIGH(); i2c_delay();
        if (SDA_READ()) { data |= (1 << i); }
        SCL_LOW(); i2c_delay();
    }
    sda_output();
    if (nack) { SDA_HIGH(); } else { SDA_LOW(); }
    i2c_delay(); SCL_HIGH(); i2c_delay(); SCL_LOW(); i2c_delay();
    SDA_HIGH();
    return data;
}

/**
 * @brief Burst-read FT6336U registers starting at reg.
 * @param[in]  reg Starting register.
 * @param[out] buf Destination buffer.
 * @param[in]  len Number of bytes.
 * @return true on success, false on NACK.
 * @req REQ-ROVARI-TOUCH-0025
 */
static bool ft_read_regs(uint8_t reg, uint8_t* buf, int len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    SEVS_INVARIANT(len > 0 && len <= FT_TOUCH_BURST_LEN);
    i2c_start();
    if (i2c_write_byte((FT6336U_ADDR << 1) | 0)) { i2c_stop(); return false; }
    if (i2c_write_byte(reg))                       { i2c_stop(); return false; }

    i2c_start();
    if (i2c_write_byte((FT6336U_ADDR << 1) | 1)) { i2c_stop(); return false; }
    for (int i = 0; i < len; i++) {
        buf[i] = i2c_read_byte(i == len - 1 ? 1 : 0);
    }
    i2c_stop();
    return true;
}

/**
 * @brief Initialize the touch controller GPIO lines.
 */
static void touch_gpio_init(void)
{
    GPIO_InitTypeDef g = {0};
    RCC_APB2PeriphClockCmd(TOUCH_I2C_GPIO_RCC | TOUCH_INT_RCC | TOUCH_RST_RCC, ENABLE);

    g.GPIO_Pin   = TOUCH_SCL_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TOUCH_SCL_PORT, &g);

    g.GPIO_Pin = TOUCH_SDA_PIN;
    GPIO_Init(TOUCH_SDA_PORT, &g);

    g.GPIO_Pin  = TOUCH_INT_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(TOUCH_INT_PORT, &g);

    g.GPIO_Pin   = TOUCH_RST_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TOUCH_RST_PORT, &g);

    SCL_HIGH();
    SDA_HIGH();
}

/**
 * @brief Pulse the controller's hardware reset line.
 */
static void touch_hw_reset(void)
{
    GPIO_WriteBit(TOUCH_RST_PORT, TOUCH_RST_PIN, Bit_SET);   Delay_Ms(10);
    GPIO_WriteBit(TOUCH_RST_PORT, TOUCH_RST_PIN, Bit_RESET); Delay_Ms(50);
    GPIO_WriteBit(TOUCH_RST_PORT, TOUCH_RST_PIN, Bit_SET);   Delay_Ms(200);
}

/* ===================================================================
 *  Public API
 * =================================================================== */

/**
 * @brief Initialize the FT6336U touch controller.
 * @return true once initialization has run (chip ID is logged).
 * @req REQ-ROVARI-TOUCH-0021
 */
bool touch_init(void)
{
    touch_gpio_init();
    touch_hw_reset();

    uint8_t chip_id = touch_read_chip_id();
    printf("Touch chip ID: 0x%02X", chip_id);
    if (chip_id == 0x64 || chip_id == 0x00) {
        printf(" (FT6336U OK)\r\n");
    } else {
        printf(" (unexpected, may still work)\r\n");
    }

    return true;
}

/**
 * @brief Read the FT6336U chip identifier.
 * @return The chip ID byte.
 * @req REQ-ROVARI-TOUCH-0023
 */
uint8_t touch_read_chip_id(void)
{
    uint8_t id = 0;
    (void)ft_read_regs(FT_REG_CHIP_ID, &id, 1);
    return id;
}

/**
 * @brief Read current touch points (raw panel coordinates).
 *
 * Reads both points in one burst and sorts them by stable finger ID so
 * point 1 always carries the lower ID.
 *
 * @param[out] data Receives the touch state.
 * @req REQ-ROVARI-TOUCH-0022
 * @req REQ-ROVARI-TOUCH-0025
 * @req REQ-ROVARI-TOUCH-WORKAROUND-001
 */
void touch_read(TouchData* data)
{
    SEVS_REQUIRE_NOT_NULL(data);
    uint8_t buf[FT_TOUCH_BURST_LEN];

    memset(data, 0, sizeof(TouchData));

    if (!ft_read_regs(FT_REG_TD_STATUS, buf, FT_TOUCH_BURST_LEN)) {
        return;
    }

    uint8_t touches = buf[0] & 0x0F;

    if (touches == 0 || touches > 2) {
        data->touched     = false;
        data->num_touches = 0;
        return;
    }

    data->touched     = true;
    data->num_touches = touches;
    SEVS_INVARIANT(touches == 1 || touches == 2);

    /* Slot A (registers 0x03-0x06 = buf[1..4]) */
    uint16_t ax = (uint16_t)(((buf[1] & 0x0F) << 8) | buf[2]);
    uint16_t ay = (uint16_t)(((buf[3] & 0x0F) << 8) | buf[4]);
    uint8_t  a_id = (buf[3] >> 4) & 0x0F;

    if (touches == 1) {
        data->x  = ax;
        data->y  = ay;
        data->id = a_id;
        return;
    }

    /* Slot B (registers 0x09-0x0C = buf[7..10]) */
    uint16_t bx = (uint16_t)(((buf[7] & 0x0F) << 8) | buf[8]);
    uint16_t by = (uint16_t)(((buf[9] & 0x0F) << 8) | buf[10]);
    uint8_t  b_id = (buf[9] >> 4) & 0x0F;

    /* Sort by ID: P1 gets lower ID, P2 gets higher ID. */
    if (a_id <= b_id) {
        data->x  = ax;  data->y  = ay;  data->id  = a_id;
        data->x2 = bx;  data->y2 = by;  data->id2 = b_id;
    } else {
        data->x  = bx;  data->y  = by;  data->id  = b_id;
        data->x2 = ax;  data->y2 = ay;  data->id2 = a_id;
    }
}

/**
 * @brief Set the touch rotation (overrides the weak stub).
 * @param[in] rotation Rotation 0-3.
 * @req REQ-ROVARI-TOUCH-0010
 */
void touch_set_rotation(uint8_t rotation)
{
    s_touch_rotation = rotation & 0x03;
}

/**
 * @brief Map raw panel coordinates to screen coordinates by rotation.
 * @param[in]  rx Raw X (0-319).
 * @param[in]  ry Raw Y (0-479).
 * @param[out] sx Receives screen X.
 * @param[out] sy Receives screen Y.
 * @req REQ-ROVARI-TOUCH-0024
 */
static void remap_touch(uint16_t rx, uint16_t ry,
                        uint16_t* sx, uint16_t* sy)
{
    SEVS_REQUIRE_NOT_NULL(sx);
    SEVS_REQUIRE_NOT_NULL(sy);
    switch (s_touch_rotation) {
        case 0:
            *sx = rx;
            *sy = ry;
            break;
        case 1:
            *sx = ry;
            *sy = (rx <= TOUCH_X_MAX) ? (uint16_t)(TOUCH_X_MAX - rx) : 0;
            break;
        case 2:
            *sx = (rx <= TOUCH_X_MAX) ? (uint16_t)(TOUCH_X_MAX - rx) : 0;
            *sy = (ry <= TOUCH_Y_MAX) ? (uint16_t)(TOUCH_Y_MAX - ry) : 0;
            break;
        case 3:
            *sx = (ry <= TOUCH_Y_MAX) ? (uint16_t)(TOUCH_Y_MAX - ry) : 0;
            *sy = rx;
            break;
        default:
            *sx = rx;
            *sy = ry;
            break;
    }
}

/**
 * @brief Read touch points and map them to screen coordinates.
 * @param[out] data Receives rotation-mapped touch state.
 * @req REQ-ROVARI-TOUCH-0024
 * @req REQ-ROVARI-TOUCH-0025
 */
void touch_read_lcd(TouchData* data)
{
    SEVS_REQUIRE_NOT_NULL(data);
    touch_read(data);

    if (!data->touched) {
        return;
    }

    uint16_t rx = data->x;
    uint16_t ry = data->y;
    remap_touch(rx, ry, &data->x, &data->y);

    if (data->num_touches >= 2) {
        rx = data->x2;
        ry = data->y2;
        remap_touch(rx, ry, &data->x2, &data->y2);
    }
}
