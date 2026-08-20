/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_spi.c
 * @brief SPI master implementation for CH32V307.
 *
 * Default pins: SPI1 SCK=PA5/MOSI=PA7/MISO=PA6 (APB2); SPI2 PB13/PB15/PB14
 * and SPI3 PB3/PB5/PB4 (APB1). Clock = f_bus / prescaler (power-of-two).
 * TXE/RXNE polling is bounded so a stalled bus cannot hang the CPU.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_spi.h"
#include "rovari_gpio.h"

/* Bounded poll cap for TXE/RXNE waits. */
#define SPI_TIMEOUT  100000U
#define SPI_PRESCALER_COUNT 8

/* Instance lookup */
typedef struct {
    SPI_TypeDef*  periph;
    uint32_t      rcc_apb;
    uint8_t       apb_bus;      /* 1 = APB1, 2 = APB2 */
    pin_t         sck_pin;
    pin_t         mosi_pin;
    pin_t         miso_pin;
} spi_def_t;

static const spi_def_t spi_defs[] = {
    [0] = {0},
    [1] = { SPI1, RCC_APB2Periph_SPI1, 2, PA5,  PA7,  PA6  },
    [2] = { SPI2, RCC_APB1Periph_SPI2, 1, PB13, PB15, PB14 },
    [3] = { SPI3, RCC_APB1Periph_SPI3, 1, PB3,  PB5,  PB4  },
};

#define SPI_DEF_COUNT (sizeof(spi_defs) / sizeof(spi_defs[0]))

/**
 * @brief Resolve an instance selector to its definition, bounded.
 * @req REQ-ROVARI-SPI-0010
 */
static const spi_def_t* get_def(SpiInstance inst)
{
    if (inst == 0 || inst >= SPI_DEF_COUNT) {
        return &spi_defs[1];
    }
    return &spi_defs[inst];
}

/**
 * @brief Find the largest prescaler whose clock does not exceed speed_hz.
 * @req REQ-ROVARI-SPI-0010
 */
static uint16_t find_prescaler(uint32_t bus_hz, uint32_t speed_hz)
{
    static const uint16_t prescaler_reg[] = {
        SPI_BaudRatePrescaler_2,   SPI_BaudRatePrescaler_4,
        SPI_BaudRatePrescaler_8,   SPI_BaudRatePrescaler_16,
        SPI_BaudRatePrescaler_32,  SPI_BaudRatePrescaler_64,
        SPI_BaudRatePrescaler_128, SPI_BaudRatePrescaler_256,
    };
    static const uint16_t dividers[] = { 2, 4, 8, 16, 32, 64, 128, 256 };

    SEVS_INVARIANT(speed_hz > 0U);
    for (int i = 0; i < SPI_PRESCALER_COUNT; i++) {
        if (bus_hz / dividers[i] <= speed_hz) {
            return prescaler_reg[i];
        }
    }
    return SPI_BaudRatePrescaler_256;  /* slowest fallback */
}

/**
 * @brief Return the APB bus clock feeding the given SPI instance.
 * @req REQ-ROVARI-SPI-0010
 */
static uint32_t get_bus_clock(uint8_t apb_bus)
{
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    return (apb_bus == 2) ? clk.PCLK2_Frequency : clk.PCLK1_Frequency;
}

/* -----------------------------------------------------------------------
 *  Public C API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize an SPI peripheral in master mode.
 * @param[in] inst      SPI instance selector (SPI_1, SPI_2, SPI_3).
 * @param[in] speed_hz  Target bit clock in Hz.
 * @param[in] mode      SPI mode 0-3 (CPOL in bit1, CPHA in bit0).
 * @param[in] lsb_first Non-zero transmits LSB first.
 * @req REQ-ROVARI-SPI-0010
 */
void spi_init(SpiInstance inst, uint32_t speed_hz, uint8_t mode, uint8_t lsb_first)
{
    const spi_def_t* def = get_def(inst);
    SEVS_INVARIANT(def->periph != NULL);

    /* Enable SPI clock */
    if (def->apb_bus == 2) {
        RCC_APB2PeriphClockCmd(def->rcc_apb, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(def->rcc_apb, ENABLE);
    }

    /* Configure pins */
    pin_mode(def->sck_pin,  AF_PushPull);
    pin_mode(def->mosi_pin, AF_PushPull);
    pin_mode(def->miso_pin, Input);

    /* Determine CPOL and CPHA from mode number */
    uint16_t cpol = (mode & 0x02) ? SPI_CPOL_High : SPI_CPOL_Low;
    uint16_t cpha = (mode & 0x01) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;

    /* Find prescaler */
    uint32_t bus_hz = get_bus_clock(def->apb_bus);
    uint16_t prescaler = find_prescaler(bus_hz, speed_hz);

    /* SPI configuration */
    SPI_InitTypeDef spi = {0};
    spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_CPOL              = cpol;
    spi.SPI_CPHA              = cpha;
    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = prescaler;
    spi.SPI_FirstBit          = lsb_first ? SPI_FirstBit_LSB : SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial     = 7;

    SPI_Init(def->periph, &spi);
    SPI_Cmd(def->periph, ENABLE);
}

/**
 * @brief Exchange one byte (full duplex).
 * @param[in] inst    SPI instance selector.
 * @param[in] tx_byte Byte to transmit.
 * @return Byte received, or 0xFF if the bus did not become ready.
 * @req REQ-ROVARI-SPI-0011
 * @req REQ-ROVARI-SPI-0020
 */
uint8_t spi_transfer(SpiInstance inst, uint8_t tx_byte)
{
    SPI_TypeDef* periph = get_def(inst)->periph;
    SEVS_INVARIANT(periph != NULL);

    /* Wait for TX buffer empty (bounded) */
    uint8_t txe_ok = 0;
    for (uint32_t i = 0U; i < SPI_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(periph, SPI_I2S_FLAG_TXE) != RESET) {
            txe_ok = 1;
            break;
        }
    }
    if (!txe_ok) {
        return 0xFF;  /* Timeout */
    }
    SPI_I2S_SendData(periph, tx_byte);

    /* Wait for RX buffer full (bounded) */
    for (uint32_t i = 0U; i < SPI_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(periph, SPI_I2S_FLAG_RXNE) != RESET) {
            return (uint8_t)SPI_I2S_ReceiveData(periph);
        }
    }
    return 0xFF;  /* Timeout */
}

/**
 * @brief Exchange len bytes in place.
 * @param[in,out] buf Buffer of bytes to send; overwritten with received data.
 * @param[in]     len Number of bytes.
 * @param[in]     inst SPI instance selector.
 * @req REQ-ROVARI-SPI-0012
 * @req REQ-ROVARI-SPI-0021
 */
void spi_transfer_buf(SpiInstance inst, uint8_t* buf, uint16_t len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(inst, buf[i]);
    }
}

/**
 * @brief Transmit one byte, discarding the received byte.
 * @param[in] inst SPI instance selector.
 * @param[in] byte Byte to transmit.
 * @req REQ-ROVARI-SPI-0013
 */
void spi_write(SpiInstance inst, uint8_t byte)
{
    (void)spi_transfer(inst, byte);
}

/**
 * @brief Transmit len bytes, discarding received data.
 * @param[in] inst SPI instance selector.
 * @param[in] buf  Bytes to transmit.
 * @param[in] len  Number of bytes.
 * @req REQ-ROVARI-SPI-0013
 * @req REQ-ROVARI-SPI-0021
 */
void spi_write_buf(SpiInstance inst, const uint8_t* buf, uint16_t len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    for (uint16_t i = 0; i < len; i++) {
        (void)spi_transfer(inst, buf[i]);
    }
}

/**
 * @brief Receive one byte by clocking out 0xFF.
 * @param[in] inst SPI instance selector.
 * @return Byte received.
 * @req REQ-ROVARI-SPI-0014
 */
uint8_t spi_read(SpiInstance inst)
{
    return spi_transfer(inst, 0xFF);
}

/**
 * @brief Receive len bytes by clocking out 0xFF.
 * @param[in]  inst SPI instance selector.
 * @param[out] buf  Buffer receiving the data.
 * @param[in]  len  Number of bytes.
 * @req REQ-ROVARI-SPI-0014
 * @req REQ-ROVARI-SPI-0021
 */
void spi_read_buf(SpiInstance inst, uint8_t* buf, uint16_t len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(inst, 0xFF);
    }
}
