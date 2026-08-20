/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_i2c.c
 * @brief I2C master implementation for CH32V307 (SEVS-Core conformant).
 *
 * Default pin mapping:
 *   I2C_1: SDA=PB7,  SCL=PB6   (APB1)
 *   I2C_2: SDA=PB11, SCL=PB10  (APB1)
 *   I2C_1_ALT: SDA=PB9, SCL=PB8 (I2C1 remapped)
 *
 * Both I2C peripherals are on APB1 (72 MHz at default 144 MHz SYSCLK).
 * Standard mode: 100 kHz, Fast mode: 400 kHz.
 *
 * The WCH I2C HAL uses an event-driven state machine. Each step in a
 * transaction (START, address, data, STOP) requires checking a specific
 * event flag. This wrapper drives the complete state machine with bounded
 * polling so no operation can hang the CPU under any bus condition.
 *
 * Timeout strategy: every hardware-polling wait is a bounded for-loop with
 * a statically-provable iteration cap (I2C_TIMEOUT). At 144 MHz, 50000
 * iterations is roughly 1-2 ms, ample for any single I2C event at 100/400
 * kHz. On timeout or bus error the driver issues STOP, software-resets the
 * peripheral, and returns a distinct non-zero phase code.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_i2c.h"

/* Timeout for event polling (bounded-loop iteration cap) */
#define I2C_TIMEOUT  50000U

/* Instance lookup */
typedef struct {
    I2C_TypeDef*  periph;
    uint32_t      rcc_apb;
    uint32_t      rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t      scl_pin;
    uint16_t      sda_pin;
    uint8_t       remap;        /* 1 = apply GPIO_Remap_I2C1 (PB8/PB9) */
} i2c_def_t;

static const i2c_def_t i2c_defs[] = {
    [0] = {0},
    [1] = { I2C1, RCC_APB1Periph_I2C1, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_6, GPIO_Pin_7, 0 },
    [2] = { I2C2, RCC_APB1Periph_I2C2, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_10, GPIO_Pin_11, 0 },
    [3] = { I2C1, RCC_APB1Periph_I2C1, RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_8, GPIO_Pin_9, 1 },
};

#define I2C_DEF_COUNT (sizeof(i2c_defs) / sizeof(i2c_defs[0]))

/**
 * @brief Resolve an instance selector to its peripheral definition.
 *
 * Out-of-range selectors fall back to I2C_1 so a misconfigured caller still
 * targets a valid peripheral rather than dereferencing past the table.
 * @req REQ-ROVARI-I2C-0022
 */
static const i2c_def_t* get_def(I2cInstance inst)
{
    if (inst == 0 || inst >= I2C_DEF_COUNT) {
        return &i2c_defs[1];
    }
    return &i2c_defs[inst];
}

/**
 * @brief Poll for an I2C event with a bounded timeout.
 * @req REQ-ROVARI-I2C-0020
 */
static uint8_t wait_event(I2C_TypeDef* periph, uint32_t event)
{
    SEVS_REQUIRE_NOT_NULL(periph);
    for (uint32_t i = 0U; i < I2C_TIMEOUT; i++) {
        if (I2C_CheckEvent(periph, event)) {
            return 0;
        }
    }
    return 1;  /* Timeout */
}

/**
 * @brief Poll until an I2C flag is set, with a bounded timeout.
 * @req REQ-ROVARI-I2C-0020
 */
static uint8_t wait_flag_clear(I2C_TypeDef* periph, uint32_t flag)
{
    SEVS_REQUIRE_NOT_NULL(periph);
    for (uint32_t i = 0U; i < I2C_TIMEOUT; i++) {
        if (I2C_GetFlagStatus(periph, flag) == RESET) {
            return 0;
        }
    }
    return 1;  /* Timeout: flag never cleared */
}

/**
 * @brief Issue STOP and software-reset the peripheral after an error.
 *
 * Leaves the bus in a recoverable state for the next transaction.
 * @req REQ-ROVARI-I2C-0021
 */
static void i2c_reset(I2C_TypeDef* periph)
{
    SEVS_REQUIRE_NOT_NULL(periph);
    I2C_GenerateSTOP(periph, ENABLE);
    I2C_SoftwareResetCmd(periph, ENABLE);
    I2C_SoftwareResetCmd(periph, DISABLE);
}

/* -----------------------------------------------------------------------
 *  Public API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize an I2C peripheral in master mode.
 *
 * @param[in] inst      Instance selector (I2C_1, I2C_2, or I2C_1_ALT).
 * @param[in] speed_hz  Bus clock in Hz; 100000 (standard) or 400000 (fast).
 *
 * @req REQ-ROVARI-I2C-0010
 */
void i2c_init(I2cInstance inst, uint32_t speed_hz)
{
    const i2c_def_t* def = get_def(inst);
    SEVS_INVARIANT(def != NULL);
    SEVS_INVARIANT(def->periph != NULL);

    /* Enable clocks */
    RCC_APB1PeriphClockCmd(def->rcc_apb, ENABLE);
    RCC_APB2PeriphClockCmd(def->rcc_gpio, ENABLE);

    /* Apply pin remap for the alternate mapping (I2C1 -> PB8/PB9). */
    if (def->remap) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
        GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);
    }

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

/**
 * @brief Scan the bus for devices that ACK their address.
 *
 * Probes 7-bit addresses 0x08-0x77 and records those that respond.
 *
 * @param[in]  inst       Instance selector.
 * @param[out] out_addrs  Buffer receiving the found 7-bit addresses.
 * @param[in]  max_addrs  Capacity of out_addrs.
 * @return Number of devices found (0..max_addrs).
 *
 * @req REQ-ROVARI-I2C-0011
 * @req REQ-ROVARI-I2C-WORKAROUND-001
 */
uint8_t i2c_scan(I2cInstance inst, uint8_t* out_addrs, uint8_t max_addrs)
{
    SEVS_REQUIRE_NOT_NULL(out_addrs);
    I2C_TypeDef* periph = get_def(inst)->periph;
    SEVS_INVARIANT(periph != NULL);
    uint8_t count = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (count >= max_addrs) {
            break;
        }

        /* Wait for bus free */
        if (wait_flag_clear(periph, I2C_FLAG_BUSY)) {
            continue;
        }

        /* Generate START */
        I2C_GenerateSTART(periph, ENABLE);
        if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) {
            i2c_reset(periph);
            continue;
        }

        /* Send address with write bit */
        I2C_Send7bitAddress(periph, (uint8_t)(addr << 1), I2C_Direction_Transmitter);

        /* Probe: wait for ACK (addr matched) or AF (NACK = no device).
         * Use direct flag/event checks. Masking I2C_FLAG_AF against
         * I2C_GetLastEvent is unreliable because AF's high byte is a
         * register selector (see REQ-ROVARI-I2C-WORKAROUND-001). */
        uint8_t acked = 0;
        for (uint32_t i = 0U; i < (I2C_TIMEOUT / 5U); i++) {
            if (I2C_CheckEvent(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
                acked = 1;
                break;
            }
            if (I2C_GetFlagStatus(periph, I2C_FLAG_AF) != RESET) {
                /* NACK: no device at this address */
                I2C_ClearFlag(periph, I2C_FLAG_AF);
                break;
            }
        }

        /* Generate STOP */
        I2C_GenerateSTOP(periph, ENABLE);

        if (acked) {
            out_addrs[count++] = addr;
        }

        /* Small bounded delay between probes to let the bus settle. */
        for (volatile uint32_t d = 0U; d < 200U; d++) {
            /* spin */
        }
    }

    return count;
}

/**
 * @brief Write one byte to a register on a device.
 *
 * @param[in] inst   Instance selector.
 * @param[in] addr   7-bit device address.
 * @param[in] reg    Register address.
 * @param[in] value  Byte to write.
 * @return 0 on success; non-zero phase code on failure.
 *
 * @req REQ-ROVARI-I2C-0012
 * @req REQ-ROVARI-I2C-0021
 */
uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t value)
{
    I2C_TypeDef* periph = get_def(inst)->periph;
    SEVS_INVARIANT(periph != NULL);

    /* Wait for bus free */
    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    /* START */
    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    /* Send device address (write) */
    I2C_Send7bitAddress(periph, (uint8_t)(addr << 1), I2C_Direction_Transmitter);
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

/**
 * @brief Write len bytes beginning at a register address.
 *
 * @param[in] inst  Instance selector.
 * @param[in] addr  7-bit device address.
 * @param[in] reg   Starting register address.
 * @param[in] data  Bytes to write.
 * @param[in] len   Number of bytes; 0 is a successful no-op.
 * @return 0 on success; non-zero phase code on failure.
 *
 * @req REQ-ROVARI-I2C-0013
 * @req REQ-ROVARI-I2C-0023
 */
uint8_t i2c_write_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                       const uint8_t* data, uint16_t len)
{
    if (len == 0) { return 0; }
    SEVS_REQUIRE_NOT_NULL(data);
    I2C_TypeDef* periph = get_def(inst)->periph;
    SEVS_INVARIANT(periph != NULL);

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    I2C_Send7bitAddress(periph, (uint8_t)(addr << 1), I2C_Direction_Transmitter);
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

/**
 * @brief Read one byte from a register on a device.
 *
 * @param[in] inst  Instance selector.
 * @param[in] addr  7-bit device address.
 * @param[in] reg   Register address.
 * @return Byte read, or 0xFF on error.
 *
 * @req REQ-ROVARI-I2C-0014
 */
uint8_t i2c_read_reg(I2cInstance inst, uint8_t addr, uint8_t reg)
{
    uint8_t val = 0xFF;
    (void)i2c_read_buf(inst, addr, reg, &val, 1);
    return val;
}

/**
 * @brief Read len bytes starting at a register address.
 *
 * Uses a write of the register address, a repeated START, then a read,
 * NACKing the final byte.
 *
 * @param[in]  inst  Instance selector.
 * @param[in]  addr  7-bit device address.
 * @param[in]  reg   Starting register address.
 * @param[out] buf   Buffer receiving the data.
 * @param[in]  len   Number of bytes to read; 0 is a successful no-op.
 * @return 0 on success; non-zero phase code on failure.
 *
 * @req REQ-ROVARI-I2C-0015
 * @req REQ-ROVARI-I2C-0023
 */
uint8_t i2c_read_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                      uint8_t* buf, uint16_t len)
{
    if (len == 0) { return 0; }
    SEVS_REQUIRE_NOT_NULL(buf);
    I2C_TypeDef* periph = get_def(inst)->periph;
    SEVS_INVARIANT(periph != NULL);

    /* Phase 1: Write the register address */
    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    I2C_Send7bitAddress(periph, (uint8_t)(addr << 1), I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(periph); return 3; }

    I2C_SendData(periph, reg);
    if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(periph); return 4; }

    /* Phase 2: Repeated START and read */
    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 5; }

    I2C_Send7bitAddress(periph, (uint8_t)(addr << 1), I2C_Direction_Receiver);
    if (wait_event(periph, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { i2c_reset(periph); return 6; }

    /* Read bytes */
    for (uint16_t i = 0; i < len; i++) {
        if (i == (uint16_t)(len - 1)) {
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

/**
 * @brief Send raw bytes to a device with no register prefix.
 *
 * @param[in] inst  Instance selector.
 * @param[in] addr  7-bit device address.
 * @param[in] data  Bytes to send.
 * @param[in] len   Number of bytes; 0 is a successful no-op.
 * @return 0 on success; non-zero phase code on failure.
 *
 * @req REQ-ROVARI-I2C-0016
 * @req REQ-ROVARI-I2C-0023
 */
uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr,
                       const uint8_t* data, uint16_t len)
{
    if (len == 0) { return 0; }
    SEVS_REQUIRE_NOT_NULL(data);
    I2C_TypeDef* periph = get_def(inst)->periph;
    SEVS_INVARIANT(periph != NULL);

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    I2C_Send7bitAddress(periph, (uint8_t)(addr << 1), I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(periph); return 3; }

    for (uint16_t i = 0; i < len; i++) {
        I2C_SendData(periph, data[i]);
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(periph); return 4; }
    }

    I2C_GenerateSTOP(periph, ENABLE);
    return 0;
}

/**
 * @brief Read raw bytes from a device with no register prefix.
 *
 * NACKs the final byte.
 *
 * @param[in]  inst  Instance selector.
 * @param[in]  addr  7-bit device address.
 * @param[out] buf   Buffer receiving the data.
 * @param[in]  len   Number of bytes to read; 0 is a successful no-op.
 * @return 0 on success; non-zero phase code on failure.
 *
 * @req REQ-ROVARI-I2C-0017
 * @req REQ-ROVARI-I2C-0023
 */
uint8_t i2c_read_raw(I2cInstance inst, uint8_t addr,
                      uint8_t* buf, uint16_t len)
{
    if (len == 0) { return 0; }
    SEVS_REQUIRE_NOT_NULL(buf);
    I2C_TypeDef* periph = get_def(inst)->periph;
    SEVS_INVARIANT(periph != NULL);

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(periph); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(periph); return 2; }

    I2C_Send7bitAddress(periph, (uint8_t)(addr << 1), I2C_Direction_Receiver);
    if (wait_event(periph, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { i2c_reset(periph); return 3; }

    for (uint16_t i = 0; i < len; i++) {
        if (i == (uint16_t)(len - 1)) {
            I2C_AcknowledgeConfig(periph, DISABLE);
            I2C_GenerateSTOP(periph, ENABLE);
        }
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_RECEIVED)) { i2c_reset(periph); return 4; }
        buf[i] = I2C_ReceiveData(periph);
    }

    I2C_AcknowledgeConfig(periph, ENABLE);
    return 0;
}
