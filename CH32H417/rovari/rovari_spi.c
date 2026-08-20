/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_spi.c - SPI master implementation wrapping WCH HAL (CH32H417)
 *
 * Default pin mapping:
 *   SPI_1: SCK=PA5,  MOSI=PA7,  MISO=PA6   (HB2)  AF5
 *   SPI_2: SCK=PB13, MOSI=PB15, MISO=PB14  (HB1)  AF5
 *   SPI_3: SCK=PB3,  MOSI=PB5,  MISO=PB4   (HB1)  AF6/AF7/AF6
 *   SPI_4: SCK=PE12, MOSI=PE14, MISO=PE13  (HB1)  AF5
 *
 * Note: SPI2-4 are on HB1 and may have the same peripheral clock
 * issue seen with USART4-8 on V3F. SPI1 (HB2) is confirmed safe.
 */

#include "rovari_spi.h"
#include "rovari_gpio.h"
#include "debug.h"

/* -- Instance lookup ------------------------------------------------ */
typedef struct {
    SPI_TypeDef*  periph;
    uint32_t      rcc_periph;
    uint8_t       rcc_bus;       /* 1 = HB1, 2 = HB2 */
    pin_t         sck_pin;
    pin_t         mosi_pin;
    pin_t         miso_pin;
    uint8_t       sck_af;
    uint8_t       mosi_af;
    uint8_t       miso_af;
    uint8_t       sck_pin_source;
    uint8_t       mosi_pin_source;
    uint8_t       miso_pin_source;
    GPIO_TypeDef* sck_port;
    GPIO_TypeDef* mosi_port;
    GPIO_TypeDef* miso_port;
    uint32_t      sck_port_rcc;
    uint32_t      mosi_port_rcc;
    uint32_t      miso_port_rcc;
} SpiDef;

static const SpiDef spi_defs[] = {
    [0] = {0},
    [1] = { SPI1, RCC_HB2Periph_SPI1, 2,
            PA5, PA7, PA6,
            GPIO_AF5, GPIO_AF5, GPIO_AF5,
            GPIO_PinSource5, GPIO_PinSource7, GPIO_PinSource6,
            GPIOA, GPIOA, GPIOA,
            RCC_HB2Periph_GPIOA, RCC_HB2Periph_GPIOA, RCC_HB2Periph_GPIOA },
    [2] = { SPI2, RCC_HB1Periph_SPI2, 1,
            PB13, PB15, PB14,
            GPIO_AF5, GPIO_AF5, GPIO_AF5,
            GPIO_PinSource13, GPIO_PinSource15, GPIO_PinSource14,
            GPIOB, GPIOB, GPIOB,
            RCC_HB2Periph_GPIOB, RCC_HB2Periph_GPIOB, RCC_HB2Periph_GPIOB },
    [3] = { SPI3, RCC_HB1Periph_SPI3, 1,
            PB3, PB5, PB4,
            GPIO_AF6, GPIO_AF7, GPIO_AF6,
            GPIO_PinSource3, GPIO_PinSource5, GPIO_PinSource4,
            GPIOB, GPIOB, GPIOB,
            RCC_HB2Periph_GPIOB, RCC_HB2Periph_GPIOB, RCC_HB2Periph_GPIOB },
    [4] = { SPI4, RCC_HB1Periph_SPI4, 1,
            PE12, PE14, PE13,
            GPIO_AF5, GPIO_AF5, GPIO_AF5,
            GPIO_PinSource12, GPIO_PinSource14, GPIO_PinSource13,
            GPIOE, GPIOE, GPIOE,
            RCC_HB2Periph_GPIOE, RCC_HB2Periph_GPIOE, RCC_HB2Periph_GPIOE },
};

#define SPI_DEF_COUNT (sizeof(spi_defs) / sizeof(spi_defs[0]))

static inline const SpiDef* get_def(SpiInstance inst)
{
    if (inst == 0 || inst >= SPI_DEF_COUNT) return &spi_defs[1];
    return &spi_defs[inst];
}

/* -- Prescaler selection -------------------------------------------- */
static uint16_t find_prescaler(uint32_t bus_hz, uint32_t speed_hz)
{
    static const uint16_t prescaler_reg[] = {
        SPI_BaudRatePrescaler_Mode0,   /* /2   */
        SPI_BaudRatePrescaler_Mode1,   /* /4   */
        SPI_BaudRatePrescaler_Mode2,   /* /8   */
        SPI_BaudRatePrescaler_Mode3,   /* /16  */
        SPI_BaudRatePrescaler_Mode4,   /* /32  */
        SPI_BaudRatePrescaler_Mode5,   /* /64  */
        SPI_BaudRatePrescaler_Mode6,   /* /128 */
        SPI_BaudRatePrescaler_Mode7,   /* /256 */
    };
    static const uint16_t dividers[] = { 2, 4, 8, 16, 32, 64, 128, 256 };

    for (int i = 0; i < 8; i++) {
        if (bus_hz / dividers[i] <= speed_hz) {
            return prescaler_reg[i];
        }
    }
    return SPI_BaudRatePrescaler_Mode7;  /* slowest fallback */
}

static uint32_t get_bus_clock(uint8_t rcc_bus)
{
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    /* H417: HCLK is the bus clock for both HB1 and HB2 */
    return clk.HCLK_Frequency;
}

/* ===================================================================
 *  Public C API
 * =================================================================== */

void spi_init(SpiInstance inst, uint32_t speed_hz, uint8_t mode, uint8_t lsb_first)
{
    const SpiDef* def = get_def(inst);

    /* Enable AFIO clock */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);

    /* Enable SPI clock */
    if (def->rcc_bus == 2) {
        RCC_HB2PeriphClockCmd(def->rcc_periph, ENABLE);
    } else {
        RCC_HB1PeriphClockCmd(def->rcc_periph, ENABLE);
    }

    /* Enable GPIO port clocks */
    RCC_HB2PeriphClockCmd(def->sck_port_rcc | def->mosi_port_rcc | def->miso_port_rcc, ENABLE);

    /* Configure SCK pin: AF push-pull */
    GPIO_PinAFConfig(def->sck_port, def->sck_pin_source, def->sck_af);
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.GPIO_Pin   = (uint16_t)(1U << def->sck_pin_source);
        gpio.GPIO_Speed = GPIO_Speed_Very_High;
        gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
        GPIO_Init(def->sck_port, &gpio);
    }

    /* Configure MOSI pin: AF push-pull */
    GPIO_PinAFConfig(def->mosi_port, def->mosi_pin_source, def->mosi_af);
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.GPIO_Pin   = (uint16_t)(1U << def->mosi_pin_source);
        gpio.GPIO_Speed = GPIO_Speed_Very_High;
        gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
        GPIO_Init(def->mosi_port, &gpio);
    }

    /* Configure MISO pin: floating input with AF */
    GPIO_PinAFConfig(def->miso_port, def->miso_pin_source, def->miso_af);
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.GPIO_Pin  = (uint16_t)(1U << def->miso_pin_source);
        gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(def->miso_port, &gpio);
    }

    /* Determine CPOL and CPHA from mode number */
    uint16_t cpol = (mode & 0x02) ? SPI_CPOL_High : SPI_CPOL_Low;
    uint16_t cpha = (mode & 0x01) ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;

    /* Find prescaler */
    uint32_t bus_hz = get_bus_clock(def->rcc_bus);
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

uint8_t spi_transfer(SpiInstance inst, uint8_t tx_byte)
{
    SPI_TypeDef* periph = get_def(inst)->periph;

    while (SPI_I2S_GetFlagStatus(periph, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(periph, tx_byte);

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
