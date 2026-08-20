/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_dma.c - DMA implementation for CH32H417 (SPI TX subset)
 */

#include "rovari_dma.h"
#include "debug.h"

/* ===================================================================
 *  SPI TX via DMA (using DMAMUX)
 * =================================================================== */

#define DMAMUX_REQ_SPI1_TX   63
#define DMAMUX_REQ_SPI2_TX   65
#define DMAMUX_REQ_SPI3_TX   67
#define DMAMUX_REQ_SPI4_TX   69

typedef struct {
    SPI_TypeDef*            spi;
    DMA_Channel_TypeDef*    tx_ch;
    DMA_TypeDef*            dma_controller;
    uint32_t                tc_flag;
    uint8_t                 mux_channel;
    uint32_t                mux_request;
} SpiDmaDef;

static const SpiDmaDef spi_dma_defs[] = {
    [0] = { 0,    0,              0,    0,              0, 0 },
    [1] = { SPI1, DMA1_Channel1,  DMA1, DMA1_FLAG_TC1,  DMA_MuxChannel1, DMAMUX_REQ_SPI1_TX },
    [2] = { SPI2, DMA1_Channel2,  DMA1, DMA1_FLAG_TC2,  DMA_MuxChannel2, DMAMUX_REQ_SPI2_TX },
    [3] = { SPI3, DMA1_Channel3,  DMA1, DMA1_FLAG_TC3,  DMA_MuxChannel3, DMAMUX_REQ_SPI3_TX },
    [4] = { SPI4, DMA1_Channel4,  DMA1, DMA1_FLAG_TC4,  DMA_MuxChannel4, DMAMUX_REQ_SPI4_TX },
};

#define SPI_DMA_DEF_COUNT (sizeof(spi_dma_defs) / sizeof(spi_dma_defs[0]))

static inline const SpiDmaDef* get_spi_dma(SpiInstance inst)
{
    if (inst == 0 || inst >= SPI_DMA_DEF_COUNT) return 0;
    return &spi_dma_defs[inst];
}

void dma_spi_tx_init(SpiInstance inst)
{
    const SpiDmaDef* def = get_spi_dma(inst);
    if (!def || !def->spi) return;

    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);
    DMA_DeInit(def->tx_ch);
    DMA_MuxChannelConfig(def->mux_channel, def->mux_request);

    DMA_InitTypeDef dma = {0};
    dma.DMA_PeripheralBaseAddr = (uint32_t)&def->spi->DATAR;
    dma.DMA_Memory0BaseAddr    = (uint32_t)0;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = 0;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;

    DMA_Init(def->tx_ch, &dma);
    SPI_I2S_DMACmd(def->spi, SPI_I2S_DMAReq_Tx, ENABLE);
}

void dma_spi_tx_start(SpiInstance inst, const uint8_t* data, uint16_t len)
{
    const SpiDmaDef* def = get_spi_dma(inst);
    if (!def || !def->spi || len == 0) return;

    while (SPI_I2S_GetFlagStatus(def->spi, SPI_I2S_FLAG_BSY) == SET);

    DMA_Cmd(def->tx_ch, DISABLE);
    DMA_ClearFlag(def->dma_controller, def->tc_flag);
    def->tx_ch->MADDR = (uint32_t)data;
    def->tx_ch->CNTR  = len;
    DMA_Cmd(def->tx_ch, ENABLE);
}

uint8_t dma_spi_tx_busy(SpiInstance inst)
{
    const SpiDmaDef* def = get_spi_dma(inst);
    if (!def) return 0;
    return (DMA_GetFlagStatus(def->dma_controller, def->tc_flag) == RESET) ? 1 : 0;
}

void dma_spi_tx_wait(SpiInstance inst)
{
    const SpiDmaDef* def = get_spi_dma(inst);
    if (!def || !def->spi) return;

    while (DMA_GetFlagStatus(def->dma_controller, def->tc_flag) == RESET);
    DMA_ClearFlag(def->dma_controller, def->tc_flag);
    while (SPI_I2S_GetFlagStatus(def->spi, SPI_I2S_FLAG_BSY) == SET);
}

/* ===================================================================
 *  Auto-chaining DMA for flicker-free LCD push (SPI1 only)
 *
 *  DMA1_Channel1 TC interrupt auto-chains 64K chunks with near-zero gap.
 * =================================================================== */

static volatile uint8_t  *_chain_ptr = 0;
static volatile uint32_t  _chain_remaining = 0;
static volatile uint8_t   _chain_active = 0;
static volatile uint8_t   _chain_done = 0;

void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt("machine")));
void DMA1_Channel1_IRQHandler(void)
{
    if (DMA_GetFlagStatus(DMA1, DMA1_FLAG_TC1) != RESET) {
        DMA_ClearFlag(DMA1, DMA1_FLAG_TC1);

        if (_chain_remaining > 0) {
            uint16_t chunk = (_chain_remaining > 65534) ? 65534 : (uint16_t)_chain_remaining;
            DMA_Cmd(DMA1_Channel1, DISABLE);
            DMA1_Channel1->MADDR = (uint32_t)_chain_ptr;
            DMA1_Channel1->CNTR  = chunk;
            DMA_Cmd(DMA1_Channel1, ENABLE);
            _chain_ptr += chunk;
            _chain_remaining -= chunk;
        } else {
            _chain_done = 1;
        }
    }
}

void dma_spi_tx_start_chained(SpiInstance inst, const uint8_t* data, uint32_t total_len)
{
    const SpiDmaDef* def = get_spi_dma(inst);
    if (!def || !def->spi || total_len == 0 || inst != 1) return;

    while (SPI_I2S_GetFlagStatus(def->spi, SPI_I2S_FLAG_BSY) == SET);

    uint16_t first_chunk = (total_len > 65534) ? 65534 : (uint16_t)total_len;
    _chain_ptr = (volatile uint8_t *)(data + first_chunk);
    _chain_remaining = total_len - first_chunk;
    _chain_done = (total_len <= 65534) ? 1 : 0;
    _chain_active = 1;

    DMA_Cmd(DMA1_Channel1, DISABLE);
    DMA_ClearFlag(DMA1, DMA1_FLAG_TC1);
    DMA1_Channel1->MADDR = (uint32_t)data;
    DMA1_Channel1->CNTR  = first_chunk;
    DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, ENABLE);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    DMA_Cmd(DMA1_Channel1, ENABLE);
}

uint8_t dma_spi_tx_chain_busy(void)
{
    return !_chain_done;
}

void dma_spi_tx_chain_wait(void)
{
    while (!_chain_done);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
    DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, DISABLE);
    _chain_active = 0;
}
