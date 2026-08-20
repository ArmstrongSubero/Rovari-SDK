/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_i2c.c — I2C master implementation for CH32V307
 *
 * Default pin mapping:
 *   I2C_1: SDA=PB7,  SCL=PB6   (APB1)
 *   I2C_2: SDA=PB11, SCL=PB10  (APB1)
 *
 * Both I2C peripherals are on APB1 (72 MHz at default 144 MHz SYSCLK).
 * Standard mode: 100 kHz, Fast mode: 400 kHz.
 *
 * The WCH I2C HAL uses an event-driven state machine. Each step in a
 * transaction (START, address, data, STOP) requires checking a specific
 * event flag. This wrapper handles the complete state machine with
 * timeout protection so the bus never hangs the CPU.
 *
 * Timeout strategy: poll with a counter. At 144 MHz, a timeout of
 * 50000 iterations is roughly 1–2 ms — more than enough for any
 * single I2C event at 100/400 kHz.
 */

#include "rovari_i2c.h"
#include "debug.h"

/* ── Timeout for event polling ───────────────────────────────────────── */
#define I2C_TIMEOUT  50000

/* ── Instance lookup ─────────────────────────────────────────────────── */
typedef struct {
    I2C_TypeDef*  periph;
    uint32_t      rcc_apb;
    uint32_t      rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t      scl_pin;
    uint16_t      sda_pin;
} I2cDef;

static const I2cDef i2c_defs[] = {
    [0] = {0},
    [1] = { I2C1, RCC_APB1Periph_I2C1, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_6, GPIO_Pin_7 },
    [2] = { I2C2, RCC_APB1Periph_I2C2, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_10, GPIO_Pin_11 },
};

#define I2C_DEF_COUNT (sizeof(i2c_defs) / sizeof(i2c_defs[0]))

static inline const I2cDef* get_def(I2cInstance inst)
{
    if (inst == 0 || inst >= I2C_DEF_COUNT) return &i2c_defs[1];
    return &i2c_defs[inst];
}

/* ── Wait for event with timeout ─────────────────────────────────────── */
static uint8_t wait_event(I2C_TypeDef* periph, uint32_t event)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (!I2C_CheckEvent(periph, event)) {
        if (--timeout == 0) return 1;  /* Timeout */
    }
    return 0;
}

/* ── Wait for flag with timeout ──────────────────────────────────────── */
static uint8_t wait_flag(I2C_TypeDef* periph, uint32_t flag)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (I2C_GetFlagStatus(periph, flag) == RESET) {
        if (--timeout == 0) return 1;
    }
    return 0;
}

/* ── Wait for flag to clear ──────────────────────────────────────────── */
static uint8_t wait_flag_clear(I2C_TypeDef* periph, uint32_t flag)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (I2C_GetFlagStatus(periph, flag) != RESET) {
        if (--timeout == 0) return 1;
    }
    return 0;
}

/* ── Reset bus on error ──────────────────────────────────────────────── */
static void i2c_reset(I2C_TypeDef* periph)
{
    I2C_GenerateSTOP(periph, ENABLE);
    I2C_SoftwareResetCmd(periph, ENABLE);
    I2C_SoftwareResetCmd(periph, DISABLE);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

void i2c_init(I2cInstance inst, uint32_t speed_hz)
{
    const I2cDef* def = get_def(inst);

    /* Enable clocks */
    RCC_APB1PeriphClockCmd(def->rcc_apb, ENABLE);
    RCC_APB2PeriphClockCmd(def->rcc_gpio, ENABLE);

    /* Configure SCL and SDA as open-drain alternate function */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->scl_pin | def->sda_pin;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(def->gpio_port, &gpio);

    /* Configure I2C */
    I2C_InitTypeDef i2c = {0};
    i2c.I2C_ClockSpeed          = speed_hz;
    i2c.I2C_Mode                = I2C_Mode_I2C;
    i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1         = 0x00;   /* Master doesn't need an address */
    i2c.I2C_Ack                 = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;

    I2C_Init(def->periph, &i2c);
    I2C_Cmd(def->periph, ENABLE);
}

uint8_t i2c_scan(I2cInstance inst, uint8_t* out_addrs, uint8_t max_addrs)
{
    I2C_TypeDef* periph = get_def(inst)->periph;
    uint8_t count = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (count >= max_addrs) break;

        /* Wait for bus free */
        if (wait_flag_clear(periph, I2C_FLAG_BUSY)) continue;

        /* Generate START */
        I2C_GenerateSTART(periph, ENABLE);
        if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) {
            i2c_reset(periph);
            continue;
        }

        /* Send address with write bit */
        I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);

        /* Short timeout — quick probe */
        uint32_t timeout = I2C_TIMEOUT / 5;
        uint8_t acked = 0;
        while (timeout--) {
            uint32_t event = I2C_GetLastEvent(periph);
            if (event & I2C_FLAG_AF) {
                /* NACK — no device at this address */
                I2C_ClearFlag(periph, I2C_FLAG_AF);
                break;
            }
            if (I2C_CheckEvent(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
                acked = 1;
                break;
            }
        }

        /* Generate STOP */
        I2C_GenerateSTOP(periph, ENABLE);

        if (acked) {
            out_addrs[count++] = addr;
        }

        /* Small delay between probes to let bus settle */
        for (volatile int d = 0; d < 200; d++);
    }

    return count;
}

uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t value)
{
    I2C_TypeDef* periph = get_def(inst)->periph;

    /* Wait for bus free */
    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    /* START */
    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    /* Send device address (write) */
    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(periph); return 3; }

    /* Send register address */
    I2C_SendData(periph, reg);
    if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(periph); return 4; }

    /* Send data byte */
    I2C_SendData(periph, value);
    if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(periph); return 5; }

    /* STOP */
    I2C_GenerateSTOP(periph, ENABLE);

    return 0;
}

uint8_t i2c_write_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                       const uint8_t* data, uint16_t len)
{
    I2C_TypeDef* periph = get_def(inst)->periph;

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(periph); return 3; }

    /* Send register address */
    I2C_SendData(periph, reg);
    if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(periph); return 4; }

    /* Send data bytes */
    for (uint16_t i = 0; i < len; i++) {
        I2C_SendData(periph, data[i]);
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(periph); return 5; }
    }

    I2C_GenerateSTOP(periph, ENABLE);
    return 0;
}

uint8_t i2c_read_reg(I2cInstance inst, uint8_t addr, uint8_t reg)
{
    uint8_t val = 0xFF;
    i2c_read_buf(inst, addr, reg, &val, 1);
    return val;
}

uint8_t i2c_read_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                      uint8_t* buf, uint16_t len)
{
    I2C_TypeDef* periph = get_def(inst)->periph;

    if (len == 0) return 0;

    /* ── Phase 1: Write the register address ── */
    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(periph); return 3; }

    I2C_SendData(periph, reg);
    if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(periph); return 4; }

    /* ── Phase 2: Repeated START and read ── */
    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 5; }

    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Receiver);
    if (wait_event(periph, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { i2c_reset(periph); return 6; }

    /* Read bytes */
    for (uint16_t i = 0; i < len; i++) {
        if (i == len - 1) {
            /* Last byte: NACK + STOP */
            I2C_AcknowledgeConfig(periph, DISABLE);
            I2C_GenerateSTOP(periph, ENABLE);
        }
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_RECEIVED)) { i2c_reset(periph); return 7; }
        buf[i] = I2C_ReceiveData(periph);
    }

    /* Re-enable ACK for next transaction */
    I2C_AcknowledgeConfig(periph, ENABLE);

    return 0;
}

uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr,
                       const uint8_t* data, uint16_t len)
{
    I2C_TypeDef* periph = get_def(inst)->periph;

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(periph); return 3; }

    for (uint16_t i = 0; i < len; i++) {
        I2C_SendData(periph, data[i]);
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(periph); return 4; }
    }

    I2C_GenerateSTOP(periph, ENABLE);
    return 0;
}

uint8_t i2c_read_raw(I2cInstance inst, uint8_t addr,
                      uint8_t* buf, uint16_t len)
{
    I2C_TypeDef* periph = get_def(inst)->periph;

    if (len == 0) return 0;

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Receiver);
    if (wait_event(periph, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { i2c_reset(periph); return 3; }

    for (uint16_t i = 0; i < len; i++) {
        if (i == len - 1) {
            I2C_AcknowledgeConfig(periph, DISABLE);
            I2C_GenerateSTOP(periph, ENABLE);
        }
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_RECEIVED)) { i2c_reset(periph); return 4; }
        buf[i] = I2C_ReceiveData(periph);
    }

    I2C_AcknowledgeConfig(periph, ENABLE);
    return 0;
}
