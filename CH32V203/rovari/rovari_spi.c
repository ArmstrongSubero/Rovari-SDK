/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_spi.c — SPI master implementation wrapping WCH HAL
 *
 * Default pin mapping (CH32V307):
 *   SPI_1: SCK=PA5,  MOSI=PA7,  MISO=PA6   (APB2, 144 MHz)
 *   SPI_2: SCK=PB13, MOSI=PB15, MISO=PB14  (APB1, 72 MHz)
 *   SPI_3: SCK=PB3,  MOSI=PB5,  MISO=PB4   (APB1, 72 MHz)
 *
 * Clock prescaler calculation:
 *   SPI clock = f_bus / prescaler
 *   Prescaler is power-of-two: 2, 4, 8, 16, 32, 64, 128, 256
 *
 *   SPI1 (APB2 = 144 MHz):
 *     /2  = 72 MHz    /4  = 36 MHz    /8  = 18 MHz    /16 = 9 MHz
 *     /32 = 4.5 MHz   /64 = 2.25 MHz  /128 = 1.125 MHz  /256 = 562.5 kHz
 *
 *   SPI2/3 (APB1 = 72 MHz):
 *     /2  = 36 MHz    /4  = 18 MHz    /8  = 9 MHz     /16 = 4.5 MHz
 *     /32 = 2.25 MHz  /64 = 1.125 MHz /128 = 562.5 kHz  /256 = 281.25 kHz
 */

#include "rovari_spi.h"
#include "rovari_gpio.h"
#include "debug.h"

/* ── Instance lookup ────────────────────────────────────────────────── */
typedef struct {
    SPI_TypeDef*  periph;
    uint32_t      rcc_apb;
    uint8_t       apb_bus;      /* 1 = APB1, 2 = APB2 */
    pin_t         sck_pin;
    pin_t         mosi_pin;
    pin_t         miso_pin;
} SpiDef;

static const SpiDef spi_defs[] = {
    [0] = {0},
    [1] = { SPI1, RCC_APB2Periph_SPI1, 2, PA5,  PA7,  PA6  },
    [2] = { SPI2, RCC_APB1Periph_SPI2, 1, PB13, PB15, PB14 },
    /* No SPI3 on CH32V203 */
};

#define SPI_DEF_COUNT (sizeof(spi_defs) / sizeof(spi_defs[0]))

static inline const SpiDef* get_def(SpiInstance inst)
{
    if (inst == 0 || inst >= SPI_DEF_COUNT) return &spi_defs[1];
    return &spi_defs[inst];
}

/* ── Prescaler selection ────────────────────────────────────────────── */
/*  Find the smallest prescaler that gives a clock <= speed_hz          */

static uint16_t find_prescaler(uint32_t bus_hz, uint32_t speed_hz)
{
    /* Prescaler values map to SPI_BaudRatePrescaler_xxx constants:
     *   /2   = 0x0000,  /4   = 0x0008,  /8   = 0x0010,
     *   /16  = 0x0018,  /32  = 0x0020,  /64  = 0x0028,
     *   /128 = 0x0030,  /256 = 0x0038
     */
    static const uint16_t prescaler_reg[] = {
        SPI_BaudRatePrescaler_2,
        SPI_BaudRatePrescaler_4,
        SPI_BaudRatePrescaler_8,
        SPI_BaudRatePrescaler_16,
        SPI_BaudRatePrescaler_32,
        SPI_BaudRatePrescaler_64,
        SPI_BaudRatePrescaler_128,
        SPI_BaudRatePrescaler_256,
    };
    static const uint16_t dividers[] = { 2, 4, 8, 16, 32, 64, 128, 256 };

    for (int i = 0; i < 8; i++) {
        if (bus_hz / dividers[i] <= speed_hz) {
            return prescaler_reg[i];
        }
    }
    return SPI_BaudRatePrescaler_256;  /* slowest fallback */
}

static uint32_t get_bus_clock(uint8_t apb_bus)
{
    /* Assumes default Rovari clock config: APB2=144MHz, APB1=72MHz */
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    return (apb_bus == 2) ? clk.PCLK2_Frequency : clk.PCLK1_Frequency;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public C API
 * ═══════════════════════════════════════════════════════════════════════ */

void spi_init(SpiInstance inst, uint32_t speed_hz, uint8_t mode, uint8_t lsb_first)
{
    const SpiDef* def = get_def(inst);

    /* Enable SPI clock */
    if (def->apb_bus == 2) {
        RCC_APB2PeriphClockCmd(def->rcc_apb, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(def->rcc_apb, ENABLE);
    }

    /* Configure pins */
    pin_mode(def->sck_pin,  AF_PushPull);   /* SCK:  alternate function output */
    pin_mode(def->mosi_pin, AF_PushPull);   /* MOSI: alternate function output */
    pin_mode(def->miso_pin, Input);         /* MISO: floating input */

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
    spi.SPI_NSS               = SPI_NSS_Soft;          /* CS managed by user */
    spi.SPI_BaudRatePrescaler = prescaler;
    spi.SPI_FirstBit          = lsb_first ? SPI_FirstBit_LSB : SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial     = 7;

    SPI_Init(def->periph, &spi);
    SPI_Cmd(def->periph, ENABLE);
}

uint8_t spi_transfer(SpiInstance inst, uint8_t tx_byte)
{
    SPI_TypeDef* periph = get_def(inst)->periph;

    /* Wait for TX buffer empty */
    while (SPI_I2S_GetFlagStatus(periph, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(periph, tx_byte);

    /* Wait for RX buffer full */
    while (SPI_I2S_GetFlagStatus(periph, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(periph);
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

uint8_t spi_read(SpiInstance inst)
{
    return spi_transfer(inst, 0xFF);
}

void spi_read_buf(SpiInstance inst, uint8_t* buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(inst, 0xFF);
    }
}
