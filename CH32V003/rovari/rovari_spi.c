/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */
/**
 * @file rovari_spi.c
 * @brief SPI master for CH32V003 (SPI1 only, APB2).
 * Default pins: SCK=PC5, MOSI=PC6, MISO=PC7
 */
#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_spi.h"
#include "rovari_gpio.h"

#define SPI_TIMEOUT 100000U

void spi_init(SpiInstance inst, uint32_t speed_hz, uint8_t mode, uint8_t lsb_first)
{
    (void)inst;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 | RCC_APB2Periph_GPIOC, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    /* SCK = PC5, MOSI = PC6 as AF push-pull */
    gpio.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &gpio);
    /* MISO = PC7 as floating input */
    gpio.GPIO_Pin   = GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &gpio);

    SPI_InitTypeDef spi = {0};
    spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_FirstBit          = lsb_first ? SPI_FirstBit_LSB : SPI_FirstBit_MSB;

    /* CPOL/CPHA from mode 0-3 */
    spi.SPI_CPOL = (mode & 0x02) ? SPI_CPOL_High : SPI_CPOL_Low;
    spi.SPI_CPHA = (mode & 0x01) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;

    /* Prescaler: SPI1 on APB2 = SystemCoreClock */
    uint32_t apb_clk = SystemCoreClock;
    uint16_t psc;
    if      (speed_hz >= apb_clk / 2)   psc = SPI_BaudRatePrescaler_2;
    else if (speed_hz >= apb_clk / 4)   psc = SPI_BaudRatePrescaler_4;
    else if (speed_hz >= apb_clk / 8)   psc = SPI_BaudRatePrescaler_8;
    else if (speed_hz >= apb_clk / 16)  psc = SPI_BaudRatePrescaler_16;
    else if (speed_hz >= apb_clk / 32)  psc = SPI_BaudRatePrescaler_32;
    else if (speed_hz >= apb_clk / 64)  psc = SPI_BaudRatePrescaler_64;
    else if (speed_hz >= apb_clk / 128) psc = SPI_BaudRatePrescaler_128;
    else                                psc = SPI_BaudRatePrescaler_256;
    spi.SPI_BaudRatePrescaler = psc;

    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);
}

uint8_t spi_transfer(SpiInstance inst, uint8_t tx_byte)
{
    (void)inst;
    for (uint32_t i = 0; i < SPI_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) != RESET) break;
    }
    SPI_I2S_SendData(SPI1, tx_byte);
    for (uint32_t i = 0; i < SPI_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) != RESET) break;
    }
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

void spi_transfer_buf(SpiInstance inst, uint8_t* buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(inst, buf[i]);
    }
}

void spi_write(SpiInstance inst, uint8_t byte)
{
    (void)spi_transfer(inst, byte);
}

void spi_write_buf(SpiInstance inst, const uint8_t* buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        (void)spi_transfer(inst, buf[i]);
    }
}

uint8_t spi_read(SpiInstance inst) { return spi_transfer(inst, 0xFF); }

void spi_read_buf(SpiInstance inst, uint8_t* buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(inst, 0xFF);
    }
}
