/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_i2c.c - Hardware I2C master implementation for CH32H417
 *
 * Ported from CH32V307 with the following H417-specific changes:
 *   - RCC_APB1PeriphClockCmd -> RCC_HB1PeriphClockCmd (I2C1/2/3)
 *   - RCC_APB2PeriphClockCmd -> RCC_HB2PeriphClockCmd (GPIO, AFIO, I2C4)
 *   - GPIO_Speed_50MHz -> GPIO_Speed_Very_High
 *   - STM32F4-style GPIO_PinAFConfig() for alternate function pins
 *   - I2C_DutyCycle_2 matching WCH EVT EEPROM example
 *   - Read uses I2C_FLAG_RXNE polling (WCH pattern), not I2C_EVENT_MASTER_BYTE_RECEIVED
 *   - Init pattern matches WCH EVT hardware.c exactly
 *
 * Available instances (all on VDDIO 3.3V domain):
 *   I2C_1: SCL=PB6(AF4), SDA=PB7(AF4)   - HB1 bus, VDDIO 3.3V
 *   I2C_2: SCL=PC0(AF9), SDA=PC1(AF9)   - HB1 bus, VIO18 (1.8V default)
 *   I2C_4: SCL=PD12(AF4), SDA=PD13(AF4) - HB2 bus, VIO18 (1.8V default)
 *
 * I2C_1 is the recommended instance for 3.3V devices (PB6/PB7 are VDDIO).
 * I2C_2 and I2C_4 are on VIO18 domain and only work at 1.8V by default.
 * I2C_3 is unavailable (PA8 = TOUCH_RST, PA13/PA14 = SWD debug) and
 * silently maps to I2C_1.
 *
 * Timeout strategy: poll with counter. At 100 MHz V3F core, 50000
 * iterations is roughly 1-2 ms, enough for any single I2C event.
 */

#include "rovari_i2c.h"
#include "debug.h"

/* -- Timeout for event polling -------------------------------------- */
#define I2C_TIMEOUT  50000

/* -- Instance lookup ------------------------------------------------ */
typedef struct {
    I2C_TypeDef*  periph;
    /* RCC */
    uint32_t      rcc_periph;       /* I2C peripheral clock */
    uint8_t       rcc_bus;          /* 1 = HB1, 2 = HB2 */
    /* SCL pin */
    GPIO_TypeDef* scl_port;
    uint16_t      scl_pin;
    uint8_t       scl_pin_source;
    uint8_t       scl_af;
    uint32_t      scl_gpio_rcc;
    /* SDA pin */
    GPIO_TypeDef* sda_port;
    uint16_t      sda_pin;
    uint8_t       sda_pin_source;
    uint8_t       sda_af;
    uint32_t      sda_gpio_rcc;
} I2cDef;

static const I2cDef i2c_defs[] = {
    /* [0] = placeholder */
    {0},

    /* I2C_1: PB6(AF4)/PB7(AF4) - VDDIO 3.3V domain */
    [1] = {
        .periph       = I2C1,
        .rcc_periph   = RCC_HB1Periph_I2C1,
        .rcc_bus      = 1,
        .scl_port     = GPIOB,  .scl_pin = GPIO_Pin_6,
        .scl_pin_source = GPIO_PinSource6, .scl_af = GPIO_AF4,
        .scl_gpio_rcc = RCC_HB2Periph_GPIOB,
        .sda_port     = GPIOB,  .sda_pin = GPIO_Pin_7,
        .sda_pin_source = GPIO_PinSource7, .sda_af = GPIO_AF4,
        .sda_gpio_rcc = RCC_HB2Periph_GPIOB,
    },

    /* I2C_2: PC0(AF9)/PC1(AF9) - VIO18 domain (1.8V default) */
    [2] = {
        .periph       = I2C2,
        .rcc_periph   = RCC_HB1Periph_I2C2,
        .rcc_bus      = 1,
        .scl_port     = GPIOC,  .scl_pin = GPIO_Pin_0,
        .scl_pin_source = GPIO_PinSource0, .scl_af = GPIO_AF9,
        .scl_gpio_rcc = RCC_HB2Periph_GPIOC,
        .sda_port     = GPIOC,  .sda_pin = GPIO_Pin_1,
        .sda_pin_source = GPIO_PinSource1, .sda_af = GPIO_AF9,
        .sda_gpio_rcc = RCC_HB2Periph_GPIOC,
    },

    /* I2C_3: UNAVAILABLE - maps to I2C_1 (PB6/PB7) */
    [3] = {
        .periph       = I2C1,
        .rcc_periph   = RCC_HB1Periph_I2C1,
        .rcc_bus      = 1,
        .scl_port     = GPIOB,  .scl_pin = GPIO_Pin_6,
        .scl_pin_source = GPIO_PinSource6, .scl_af = GPIO_AF4,
        .scl_gpio_rcc = RCC_HB2Periph_GPIOB,
        .sda_port     = GPIOB,  .sda_pin = GPIO_Pin_7,
        .sda_pin_source = GPIO_PinSource7, .sda_af = GPIO_AF4,
        .sda_gpio_rcc = RCC_HB2Periph_GPIOB,
    },

    /* I2C_4: PD12(AF4)/PD13(AF4) */
    [4] = {
        .periph       = I2C4,
        .rcc_periph   = RCC_HB2Periph_I2C4,
        .rcc_bus      = 2,
        .scl_port     = GPIOD,  .scl_pin = GPIO_Pin_12,
        .scl_pin_source = GPIO_PinSource12, .scl_af = GPIO_AF4,
        .scl_gpio_rcc = RCC_HB2Periph_GPIOD,
        .sda_port     = GPIOD,  .sda_pin = GPIO_Pin_13,
        .sda_pin_source = GPIO_PinSource13, .sda_af = GPIO_AF4,
        .sda_gpio_rcc = RCC_HB2Periph_GPIOD,
    },
};

#define I2C_DEF_COUNT (sizeof(i2c_defs) / sizeof(i2c_defs[0]))

static inline const I2cDef* get_def(I2cInstance inst)
{
    if (inst == 0 || inst >= I2C_DEF_COUNT) return &i2c_defs[2];
    return &i2c_defs[inst];
}

/* -- Wait helpers with timeout -------------------------------------- */

static uint8_t wait_event(I2C_TypeDef* periph, uint32_t event)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (!I2C_CheckEvent(periph, event)) {
        if (--timeout == 0) return 1;
    }
    return 0;
}

static uint8_t wait_flag(I2C_TypeDef* periph, uint32_t flag)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (I2C_GetFlagStatus(periph, flag) == RESET) {
        if (--timeout == 0) return 1;
    }
    return 0;
}

static uint8_t wait_flag_clear(I2C_TypeDef* periph, uint32_t flag)
{
    uint32_t timeout = I2C_TIMEOUT;
    while (I2C_GetFlagStatus(periph, flag) != RESET) {
        if (--timeout == 0) return 1;
    }
    return 0;
}

/* -- Stored init speed per instance for recovery after reset -------- */
static uint32_t i2c_speed[I2C_DEF_COUNT] = {0};

static void i2c_reset(I2cInstance inst)
{
    const I2cDef* def = get_def(inst);
    I2C_TypeDef* periph = def->periph;

    I2C_GenerateSTOP(periph, ENABLE);

    /* Software reset clears all config registers */
    I2C_SoftwareResetCmd(periph, ENABLE);
    I2C_SoftwareResetCmd(periph, DISABLE);

    /* Reconfigure the peripheral (speed, ACK, mode) */
    I2C_InitTypeDef i2c = {0};
    i2c.I2C_ClockSpeed          = i2c_speed[inst] ? i2c_speed[inst] : 100000;
    i2c.I2C_Mode                = I2C_Mode_I2C;
    i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1         = 0x00;
    i2c.I2C_Ack                 = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;

    I2C_Init(periph, &i2c);
    I2C_Cmd(periph, ENABLE);

    /* Small delay for bus recovery */
    for (volatile int d = 0; d < 1000; d++);
}

/* =================================================================
 *  Public API
 * ================================================================= */

void i2c_init(I2cInstance inst, uint32_t speed_hz)
{
    const I2cDef* def = get_def(inst);

    /* Enable clocks - matches WCH EVT IIC_Init() pattern */
    RCC_HB2PeriphClockCmd(def->scl_gpio_rcc | def->sda_gpio_rcc
                          | RCC_HB2Periph_AFIO, ENABLE);

    if (def->rcc_bus == 1)
        RCC_HB1PeriphClockCmd(def->rcc_periph, ENABLE);
    else
        RCC_HB2PeriphClockCmd(def->rcc_periph, ENABLE);

    /* SCL pin: AF open-drain */
    GPIO_PinAFConfig(def->scl_port, def->scl_pin_source, def->scl_af);
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = def->scl_pin;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_Init(def->scl_port, &gpio);

    /* SDA pin: AF open-drain */
    GPIO_PinAFConfig(def->sda_port, def->sda_pin_source, def->sda_af);
    gpio.GPIO_Pin   = def->sda_pin;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_Init(def->sda_port, &gpio);

    /* Configure I2C peripheral - matches WCH EVT EEPROM example */
    I2C_InitTypeDef i2c = {0};
    i2c.I2C_ClockSpeed          = speed_hz;
    i2c.I2C_Mode                = I2C_Mode_I2C;
    i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1         = 0x00;
    i2c.I2C_Ack                 = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;

    I2C_Init(def->periph, &i2c);
    I2C_Cmd(def->periph, ENABLE);

    /* Store speed for recovery after reset */
    if (inst < I2C_DEF_COUNT)
        i2c_speed[inst] = speed_hz;
}

uint8_t i2c_scan(I2cInstance inst, uint8_t* out_addrs, uint8_t max_addrs)
{
    I2C_TypeDef* periph = get_def(inst)->periph;
    uint8_t count = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (count >= max_addrs) break;

        if (wait_flag_clear(periph, I2C_FLAG_BUSY)) continue;

        I2C_GenerateSTART(periph, ENABLE);
        if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) {
            i2c_reset(inst);
            continue;
        }

        I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);

        uint32_t timeout = I2C_TIMEOUT / 5;
        uint8_t acked = 0;
        while (timeout--) {
            uint32_t event = I2C_GetLastEvent(periph);
            if (event & I2C_FLAG_AF) {
                I2C_ClearFlag(periph, I2C_FLAG_AF);
                break;
            }
            if (I2C_CheckEvent(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
                acked = 1;
                break;
            }
        }

        I2C_GenerateSTOP(periph, ENABLE);

        if (acked) {
            out_addrs[count++] = addr;
        }

        for (volatile int d = 0; d < 200; d++);
    }

    return count;
}

uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t value)
{
    I2C_TypeDef* periph = get_def(inst)->periph;

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(inst); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(inst); return 2; }

    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(inst); return 3; }

    I2C_SendData(periph, reg);
    if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(inst); return 4; }

    I2C_SendData(periph, value);
    if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(inst); return 5; }

    I2C_GenerateSTOP(periph, ENABLE);
    return 0;
}

uint8_t i2c_write_buf(I2cInstance inst, uint8_t addr, uint8_t reg,
                       const uint8_t* data, uint16_t len)
{
    I2C_TypeDef* periph = get_def(inst)->periph;

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(inst); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(inst); return 2; }

    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(inst); return 3; }

    I2C_SendData(periph, reg);
    if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(inst); return 4; }

    for (uint16_t i = 0; i < len; i++) {
        I2C_SendData(periph, data[i]);
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(inst); return 5; }
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
    /*
     * WCH I2C peripheral does not reliably handle multi-byte burst reads.
     * WCH's own EEPROM example reads one byte per transaction in a loop.
     * We do the same: each byte gets its own START/addr/reg/rSTART/read/STOP.
     * The FT6336U (and most I2C devices) auto-increment the register address,
     * so sequential single-byte reads produce the same result as a burst.
     */
    for (uint16_t i = 0; i < len; i++) {
        I2C_TypeDef* periph = get_def(inst)->periph;

        /* Phase 1: Write register address */
        if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(inst); return 1; }

        I2C_GenerateSTART(periph, ENABLE);
        if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(inst); return 2; }

        I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);
        if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(inst); return 3; }

        I2C_SendData(periph, reg + i);
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(inst); return 4; }

        /* Phase 2: Repeated START, read single byte, NACK+STOP */
        I2C_GenerateSTART(periph, ENABLE);
        if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(inst); return 5; }

        I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Receiver);
        if (wait_event(periph, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { i2c_reset(inst); return 6; }

        I2C_AcknowledgeConfig(periph, DISABLE);
        I2C_GenerateSTOP(periph, ENABLE);
        if (wait_flag(periph, I2C_FLAG_RXNE)) { i2c_reset(inst); return 7; }
        buf[i] = I2C_ReceiveData(periph);

        I2C_AcknowledgeConfig(periph, ENABLE);
    }

    return 0;
}

uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr,
                       const uint8_t* data, uint16_t len)
{
    I2C_TypeDef* periph = get_def(inst)->periph;

    if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(inst); return 1; }

    I2C_GenerateSTART(periph, ENABLE);
    if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(inst); return 2; }

    I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Transmitter);
    if (wait_event(periph, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { i2c_reset(inst); return 3; }

    for (uint16_t i = 0; i < len; i++) {
        I2C_SendData(periph, data[i]);
        if (wait_event(periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) { i2c_reset(inst); return 4; }
    }

    I2C_GenerateSTOP(periph, ENABLE);
    return 0;
}

uint8_t i2c_read_raw(I2cInstance inst, uint8_t addr,
                      uint8_t* buf, uint16_t len)
{
    /*
     * WCH I2C: read one byte per transaction for reliability.
     * Raw reads have no register address, just START/addr+R/read/STOP.
     */
    for (uint16_t i = 0; i < len; i++) {
        I2C_TypeDef* periph = get_def(inst)->periph;

        if (wait_flag_clear(periph, I2C_FLAG_BUSY)) { i2c_reset(inst); return 1; }

        I2C_GenerateSTART(periph, ENABLE);
        if (wait_event(periph, I2C_EVENT_MASTER_MODE_SELECT)) { i2c_reset(inst); return 2; }

        I2C_Send7bitAddress(periph, addr << 1, I2C_Direction_Receiver);
        if (wait_event(periph, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) { i2c_reset(inst); return 3; }

        I2C_AcknowledgeConfig(periph, DISABLE);
        I2C_GenerateSTOP(periph, ENABLE);
        if (wait_flag(periph, I2C_FLAG_RXNE)) { i2c_reset(inst); return 4; }
        buf[i] = I2C_ReceiveData(periph);

        I2C_AcknowledgeConfig(periph, ENABLE);
    }

    return 0;
}
