/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */
/**
 * @file rovari_i2c.c
 * @brief I2C master for CH32V003 (I2C1 only, APB1).
 * Default pins: SCL=PC2, SDA=PC1
 */
#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_i2c.h"
#include "rovari_gpio.h"
#include "rovari_uart.h"

#define I2C_TIMEOUT 50000U

static uint8_t wait_event(uint32_t event)
{
    for (uint32_t i = 0; i < I2C_TIMEOUT; i++) {
        if (I2C_CheckEvent(I2C1, event)) return 1;
    }
    return 0;
}

void i2c_init(I2cInstance inst, uint32_t speed_hz)
{
    (void)inst;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    /* SCL=PC2, SDA=PC1 as open-drain AF */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = GPIO_Pin_2 | GPIO_Pin_1;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &gpio);

    I2C_InitTypeDef i2c = {0};
    i2c.I2C_ClockSpeed          = speed_hz;
    i2c.I2C_Mode                = I2C_Mode_I2C;
    i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1        = 0x00;
    i2c.I2C_Ack                 = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &i2c);
    I2C_Cmd(I2C1, ENABLE);
}

uint8_t i2c_write_reg(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t data)
{
    (void)inst;
    I2C_GenerateSTART(I2C1, ENABLE);
    if (!wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 0;
    I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Transmitter);
    if (!wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 0;
    I2C_SendData(I2C1, reg);
    if (!wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 0;
    I2C_SendData(I2C1, data);
    if (!wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 0;
    I2C_GenerateSTOP(I2C1, ENABLE);
    return 1;
}

uint8_t i2c_read_reg(I2cInstance inst, uint8_t addr, uint8_t reg)
{
    (void)inst;
    I2C_GenerateSTART(I2C1, ENABLE);
    if (!wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 0;
    I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Transmitter);
    if (!wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 0;
    I2C_SendData(I2C1, reg);
    if (!wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 0;

    I2C_GenerateSTART(I2C1, ENABLE);
    if (!wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 0;
    I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Receiver);
    if (!wait_event(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) return 0;
    I2C_AcknowledgeConfig(I2C1, DISABLE);
    I2C_GenerateSTOP(I2C1, ENABLE);
    if (!wait_event(I2C_EVENT_MASTER_BYTE_RECEIVED)) return 0;
    uint8_t data = I2C_ReceiveData(I2C1);
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    return data;
}

uint8_t i2c_write_buf(I2cInstance inst, uint8_t addr, uint8_t reg, const uint8_t* data, uint16_t len)
{
    (void)inst;
    I2C_GenerateSTART(I2C1, ENABLE);
    if (!wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 0;
    I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Transmitter);
    if (!wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 0;
    I2C_SendData(I2C1, reg);
    if (!wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 0;
    for (uint16_t i = 0; i < len; i++) {
        I2C_SendData(I2C1, data[i]);
        if (!wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 0;
    }
    I2C_GenerateSTOP(I2C1, ENABLE);
    return 1;
}

uint8_t i2c_read_buf(I2cInstance inst, uint8_t addr, uint8_t reg, uint8_t* data, uint16_t len)
{
    (void)inst;
    if (len == 0) return 0;
    I2C_GenerateSTART(I2C1, ENABLE);
    if (!wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 0;
    I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Transmitter);
    if (!wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 0;
    I2C_SendData(I2C1, reg);
    if (!wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 0;
    I2C_GenerateSTART(I2C1, ENABLE);
    if (!wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 0;
    I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Receiver);
    if (!wait_event(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) return 0;
    for (uint16_t i = 0; i < len; i++) {
        if (i == len - 1) {
            I2C_AcknowledgeConfig(I2C1, DISABLE);
            I2C_GenerateSTOP(I2C1, ENABLE);
        }
        if (!wait_event(I2C_EVENT_MASTER_BYTE_RECEIVED)) return 0;
        data[i] = I2C_ReceiveData(I2C1);
    }
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    return 1;
}

uint8_t i2c_write_raw(I2cInstance inst, uint8_t addr, const uint8_t* data, uint16_t len)
{
    (void)inst;
    I2C_GenerateSTART(I2C1, ENABLE);
    if (!wait_event(I2C_EVENT_MASTER_MODE_SELECT)) return 0;
    I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Transmitter);
    if (!wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return 0;
    for (uint16_t i = 0; i < len; i++) {
        I2C_SendData(I2C1, data[i]);
        if (!wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return 0;
    }
    I2C_GenerateSTOP(I2C1, ENABLE);
    return 1;
}

void i2c_scan(I2cInstance inst)
{
    (void)inst;
    for (uint8_t addr = 1; addr < 128; addr++) {
        I2C_GenerateSTART(I2C1, ENABLE);
        uint8_t found = 0;
        if (wait_event(I2C_EVENT_MASTER_MODE_SELECT)) {
            I2C_Send7bitAddress(I2C1, addr << 1, I2C_Direction_Transmitter);
            found = wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
        }
        I2C_GenerateSTOP(I2C1, ENABLE);
        Delay_Ms(1);
        if (found) {
            uart_printf(SERIAL1, "I2C: 0x%02X\r\n", addr);
        }
    }
}
