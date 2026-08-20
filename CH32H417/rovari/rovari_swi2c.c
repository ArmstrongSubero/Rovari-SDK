/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_swi2c.c - Software (bit-bang) I2C master driver
 *
 * Pin-configurable software I2C. Works on any GPIO pin regardless of
 * I/O voltage domain. Extracted from the FT6336U touch driver's proven
 * bit-bang implementation and generalized for any I2C device.
 *
 * On CH32H417, hardware I2C default pins (PB6/PB7 for I2C1) are on the
 * VIO18 domain and output 1.4V, making them unusable for 3.3V devices.
 * Software I2C lets you pick pins on the VDDIO (3.3V) domain.
 */

#include "rovari_swi2c.h"
#include "debug.h"

/* -- Default half-period delay count -------------------------------- */
#define SWI2C_DEFAULT_DELAY  60

/* -- Low-level bit-bang primitives ---------------------------------- */

static void swi2c_delay(const SoftI2c* bus)
{
    volatile uint32_t i = bus->delay_count;
    while (i--);
}

#define SCL_HIGH(b) ((b)->scl_port->BSHR = (b)->scl_pin)
#define SCL_LOW(b)  ((b)->scl_port->BCR  = (b)->scl_pin)
#define SDA_HIGH(b) ((b)->sda_port->BSHR = (b)->sda_pin)
#define SDA_LOW(b)  ((b)->sda_port->BCR  = (b)->sda_pin)
#define SDA_READ(b) (GPIO_ReadInputDataBit((b)->sda_port, (b)->sda_pin))

static void sda_set_input(const SoftI2c* bus)
{
    GPIO_InitTypeDef g = {0};
    g.GPIO_Pin   = bus->sda_pin;
    g.GPIO_Mode  = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(bus->sda_port, &g);
}

static void sda_set_output(const SoftI2c* bus)
{
    GPIO_InitTypeDef g = {0};
    g.GPIO_Pin   = bus->sda_pin;
    g.GPIO_Mode  = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(bus->sda_port, &g);
}

static void swi2c_start(const SoftI2c* bus)
{
    sda_set_output(bus);
    SDA_HIGH(bus); SCL_HIGH(bus); swi2c_delay(bus);
    SDA_LOW(bus);  swi2c_delay(bus);
    SCL_LOW(bus);  swi2c_delay(bus);
}

static void swi2c_stop(const SoftI2c* bus)
{
    sda_set_output(bus);
    SCL_LOW(bus); SDA_LOW(bus); swi2c_delay(bus);
    SCL_HIGH(bus); swi2c_delay(bus);
    SDA_HIGH(bus); swi2c_delay(bus);
}

/* Returns 0 for ACK, 1 for NACK */
static uint8_t swi2c_write_byte(const SoftI2c* bus, uint8_t data)
{
    uint8_t ack;
    sda_set_output(bus);
    for (int i = 7; i >= 0; i--) {
        if (data & (1 << i)) SDA_HIGH(bus); else SDA_LOW(bus);
        swi2c_delay(bus);
        SCL_HIGH(bus); swi2c_delay(bus);
        SCL_LOW(bus);  swi2c_delay(bus);
    }
    sda_set_input(bus);
    SCL_HIGH(bus); swi2c_delay(bus);
    ack = SDA_READ(bus);
    SCL_LOW(bus);  swi2c_delay(bus);
    sda_set_output(bus);
    return ack;
}

static uint8_t swi2c_read_byte(const SoftI2c* bus, uint8_t nack)
{
    uint8_t data = 0;
    sda_set_input(bus);
    for (int i = 7; i >= 0; i--) {
        SCL_HIGH(bus); swi2c_delay(bus);
        if (SDA_READ(bus)) data |= (1 << i);
        SCL_LOW(bus); swi2c_delay(bus);
    }
    sda_set_output(bus);
    if (nack) SDA_HIGH(bus); else SDA_LOW(bus);
    swi2c_delay(bus);
    SCL_HIGH(bus); swi2c_delay(bus);
    SCL_LOW(bus);  swi2c_delay(bus);
    SDA_HIGH(bus);
    return data;
}

/* -- Port clock enable helper --------------------------------------- */
static void enable_gpio_clock(GPIO_TypeDef* port)
{
    if      (port == GPIOA) RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA, ENABLE);
    else if (port == GPIOB) RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB, ENABLE);
    else if (port == GPIOC) RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC, ENABLE);
    else if (port == GPIOD) RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOD, ENABLE);
    else if (port == GPIOE) RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOE, ENABLE);
    else if (port == GPIOF) RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOF, ENABLE);
}

/* =================================================================
 *  Public API
 * ================================================================= */

void swi2c_init(SoftI2c* bus,
                GPIO_TypeDef* scl_port, uint16_t scl_pin,
                GPIO_TypeDef* sda_port, uint16_t sda_pin)
{
    bus->scl_port    = scl_port;
    bus->scl_pin     = scl_pin;
    bus->sda_port    = sda_port;
    bus->sda_pin     = sda_pin;
    bus->delay_count = SWI2C_DEFAULT_DELAY;

    /* Enable GPIO clocks */
    enable_gpio_clock(scl_port);
    if (sda_port != scl_port)
        enable_gpio_clock(sda_port);

    /* Configure SCL as open-drain output */
    GPIO_InitTypeDef g = {0};
    g.GPIO_Pin   = scl_pin;
    g.GPIO_Mode  = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(scl_port, &g);

    /* Configure SDA as open-drain output */
    g.GPIO_Pin = sda_pin;
    GPIO_Init(sda_port, &g);

    /* Both lines idle high */
    SCL_HIGH(bus);
    SDA_HIGH(bus);
}

void swi2c_set_speed(SoftI2c* bus, uint32_t count)
{
    if (count < 10)   count = 10;
    if (count > 1000)  count = 1000;
    bus->delay_count = count;
}

uint8_t swi2c_scan(SoftI2c* bus, uint8_t* out_addrs, uint8_t max_addrs)
{
    uint8_t count = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (count >= max_addrs) break;

        swi2c_start(bus);
        uint8_t nack = swi2c_write_byte(bus, (addr << 1) | 0);
        swi2c_stop(bus);

        if (!nack) {
            out_addrs[count++] = addr;
        }
    }
    return count;
}

bool swi2c_write_reg(SoftI2c* bus, uint8_t addr, uint8_t reg,
                     const uint8_t* data, uint16_t len)
{
    swi2c_start(bus);
    if (swi2c_write_byte(bus, (addr << 1) | 0)) { swi2c_stop(bus); return false; }
    if (swi2c_write_byte(bus, reg))              { swi2c_stop(bus); return false; }

    for (uint16_t i = 0; i < len; i++) {
        if (swi2c_write_byte(bus, data[i]))      { swi2c_stop(bus); return false; }
    }

    swi2c_stop(bus);
    return true;
}

bool swi2c_read_reg(SoftI2c* bus, uint8_t addr, uint8_t reg,
                    uint8_t* buf, uint16_t len)
{
    /* Write phase: send register address */
    swi2c_start(bus);
    if (swi2c_write_byte(bus, (addr << 1) | 0)) { swi2c_stop(bus); return false; }
    if (swi2c_write_byte(bus, reg))              { swi2c_stop(bus); return false; }

    /* Read phase: repeated start */
    swi2c_start(bus);
    if (swi2c_write_byte(bus, (addr << 1) | 1)) { swi2c_stop(bus); return false; }

    for (uint16_t i = 0; i < len; i++)
        buf[i] = swi2c_read_byte(bus, (i == len - 1) ? 1 : 0);

    swi2c_stop(bus);
    return true;
}

bool swi2c_write_raw(SoftI2c* bus, uint8_t addr,
                     const uint8_t* data, uint16_t len)
{
    swi2c_start(bus);
    if (swi2c_write_byte(bus, (addr << 1) | 0)) { swi2c_stop(bus); return false; }

    for (uint16_t i = 0; i < len; i++) {
        if (swi2c_write_byte(bus, data[i]))      { swi2c_stop(bus); return false; }
    }

    swi2c_stop(bus);
    return true;
}

bool swi2c_read_raw(SoftI2c* bus, uint8_t addr,
                    uint8_t* buf, uint16_t len)
{
    swi2c_start(bus);
    if (swi2c_write_byte(bus, (addr << 1) | 1)) { swi2c_stop(bus); return false; }

    for (uint16_t i = 0; i < len; i++)
        buf[i] = swi2c_read_byte(bus, (i == len - 1) ? 1 : 0);

    swi2c_stop(bus);
    return true;
}
