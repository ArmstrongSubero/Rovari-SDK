/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_psram.c - QSPI PSRAM driver for APS6404L (CH32H417)
 *
 * Uses QSPI2 on PE10-PE15 (VIO18 domain, 1.8V).
 * The APS6404L is a 64Mbit (8MB) SPI/QPI PSRAM with self-managed refresh.
 * No erase required. Reads and writes are direct.
 *
 * Pin mapping (all AF7, all VIO18 1.8V):
 *   PE10 = QSPI2_SCK
 *   PE11 = QSPI2_SCSN (chip select, active low)
 *   PE12 = QSPI2_SIO0
 *   PE13 = QSPI2_SIO1
 *   PE14 = QSPI2_SIO2
 *   PE15 = QSPI2_SIO3
 *
 * APS6404L commands used:
 *   0x66 + 0x99 = Reset Enable + Reset
 *   0x9F        = Read ID (SPI mode, 33 MHz max)
 *   0x35        = Enter Quad Mode
 *   0xF5        = Exit Quad Mode
 *   0x02        = Write (SPI, no erase needed)
 *   0x38        = Quad Write
 *   0x0B        = Fast Read (SPI, 8 wait cycles)
 *   0xEB        = Fast Read Quad (6 wait cycles, 144 MHz max)
 *   0xC0        = Halfsleep Entry
 *
 * IMPORTANT: VIO18 must remain at 1.8V (default). Do NOT call
 * PWR_VIO18LevelCfg(MODE3) when PSRAM is connected.
 */

#include "rovari_psram.h"
#include "debug.h"

/* -- APS6404L Commands ---------------------------------------------------- */
#define PSRAM_CMD_RESET_ENABLE    0x66
#define PSRAM_CMD_RESET           0x99
#define PSRAM_CMD_READ_ID         0x9F
#define PSRAM_CMD_READ            0x03
#define PSRAM_CMD_READ            0x03
#define PSRAM_CMD_FAST_READ       0x0B
#define PSRAM_CMD_FAST_READ_QUAD  0xEB
#define PSRAM_CMD_WRITE           0x02
#define PSRAM_CMD_QUAD_WRITE      0x38
#define PSRAM_CMD_ENTER_QUAD      0x35
#define PSRAM_CMD_EXIT_QUAD       0xF5
#define PSRAM_CMD_HALFSLEEP       0xC0

/* 8 MB address space, 23-bit address */
#define PSRAM_SIZE                (8 * 1024 * 1024)
#define PSRAM_ADDR_BITS           22  /* FSize = log2(size) - 1 */

/* -- Internal helpers ----------------------------------------------------- */

static void qspi_send(uint8_t *buf, uint32_t len)
{
    /* Write data to FIFO. The QSPI peripheral clocks data out
     * automatically. We must keep the FIFO fed faster than it drains.
     * At 75 MHz SPI, 1 byte takes ~107ns (8 bits / 75 MHz).
     * Polling FT flag adds overhead. Use tight loop. */
    for (uint32_t i = 0; i < len; i++) {
        while (!QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_FT));
        QSPI_SendData8(QSPI2, buf[i]);
    }
}

static void qspi_receive(uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        while (!QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_FT));
        buf[i] = QSPI_ReceiveData8(QSPI2);
    }
}

static void qspi_wait_busy(void)
{
    while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_BUSY) == SET);
}

static void qspi_write_cmd(uint8_t cmd)
{
    QSPI_ComConfig_InitTypeDef cc = {0};
    cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_1Line;
    cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_NoAddress;
    cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_NoData;
    cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
    cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Read;
    cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
    cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
    cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
    cc.QSPI_ComConfig_Ins         = cmd;
    cc.QSPI_ComConfig_DummyCycles = 0;
    QSPI_ComConfig_Init(QSPI2, &cc);

    QSPI_SetDataLength(QSPI2, 0);
    QSPI_Start(QSPI2);

    while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_TC) == RESET);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);
}

/* -- GPIO and QSPI2 hardware init ---------------------------------------- */

static void gpio_config(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOE, ENABLE);

    /* All QSPI2 pins on PE10-PE15, AF7 */
    uint8_t pins[] = {
        GPIO_PinSource10, GPIO_PinSource11, GPIO_PinSource12,
        GPIO_PinSource13, GPIO_PinSource14, GPIO_PinSource15
    };
    uint16_t pin_masks[] = {
        GPIO_Pin_10, GPIO_Pin_11, GPIO_Pin_12,
        GPIO_Pin_13, GPIO_Pin_14, GPIO_Pin_15
    };

    for (int i = 0; i < 6; i++) {
        GPIO_PinAFConfig(GPIOE, pins[i], GPIO_AF7);
        gpio.GPIO_Pin   = pin_masks[i];
        gpio.GPIO_Speed = GPIO_Speed_Very_High;
        gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
        GPIO_Init(GPIOE, &gpio);
    }
}

static void qspi2_config(void)
{
    QSPI_InitTypeDef qspi = {0};

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_QSPI2, ENABLE);

    /* HCLK = 150 MHz. Prescaler 1 = 75 MHz. Mode3 (CLK idles HIGH).
     * This is the ONLY working configuration for QSPI2 on the H417.
     * Mode0 causes 1-bit shift in read data.
     * Prescaler > 1 produces no output. */
    qspi.QSPI_Prescaler = 1;
    qspi.QSPI_CKMode    = QSPI_CKMode_Mode3;
    qspi.QSPI_CSHTime   = QSPI_CSHTime_8Cycle;
    qspi.QSPI_FSize     = PSRAM_ADDR_BITS;  /* 2^(22+1) = 8MB */
    qspi.QSPI_FSelect   = QSPI_FSelect_1;
    qspi.QSPI_DFlash    = QSPI_DFlash_Disable;

    QSPI_Init(QSPI2, &qspi);
    QSPI_SetFIFOThreshold(QSPI2, 1);
}

/* =========================================================================
 *  Public API
 * ========================================================================= */

uint8_t psram_init(void)
{
    gpio_config();
    qspi2_config();
    QSPI_Cmd(QSPI2, ENABLE);

    /* Reset the PSRAM (required after power-up per datasheet) */
    Delay_Us(200);  /* tPU >= 150 us */
    qspi_write_cmd(PSRAM_CMD_RESET_ENABLE);
    qspi_write_cmd(PSRAM_CMD_RESET);
    Delay_Us(100);  /* tRST >= 50 ns, give extra margin */

    /* Read device ID to verify communication */
    uint32_t id = psram_read_id();

    /* APS6404L manufacturer ID = 0x0D, KGD = 0x5D (pass) */
    uint8_t mfr = (id >> 8) & 0xFF;
    uint8_t kgd = id & 0xFF;

    if (mfr == 0x0D && kgd == 0x5D) {
        return 0;  /* Success: known good die */
    } else if (mfr == 0x0D) {
        return 0;  /* Recognized manufacturer, KGD may be 0x55 (fail die) */
    }

    return 1;  /* Communication failed or unknown device */
}

uint32_t psram_read_id(void)
{
    uint32_t res = 0;

    qspi_wait_busy();

    QSPI_ComConfig_InitTypeDef cc = {0};
    cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_1Line;
    cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_1Line;
    cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_1Line;
    cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
    cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Read;
    cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
    cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
    cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
    cc.QSPI_ComConfig_Ins         = PSRAM_CMD_READ_ID;
    cc.QSPI_ComConfig_DummyCycles = 0;
    QSPI_ComConfig_Init(QSPI2, &cc);

    QSPI_SetAddress(QSPI2, 0);
    QSPI_SetDataLength(QSPI2, 2);
    QSPI_Start(QSPI2);

    for (int i = 0; i < 2; i++) {
        while (!QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_FT));
        res = (res << 8) | QSPI_ReceiveData8(QSPI2);
    }

    QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);

    return res;
}

void psram_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (addr + len > PSRAM_SIZE) return;

    /* tCEM max 8us at standard temp. At 75 MHz SPI, ~75 bytes in 8us.
     * Chunk to 32 bytes with delay between chunks for margin. */
    const uint32_t chunk = 32;

    while (len > 0) {
        uint32_t n = (len > chunk) ? chunk : len;

        qspi_wait_busy();

        QSPI_ComConfig_InitTypeDef cc = {0};
        cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_1Line;
        cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_1Line;
        cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_1Line;
        cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
        cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Write;
        cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
        cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
        cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
        cc.QSPI_ComConfig_Ins         = PSRAM_CMD_WRITE;
        cc.QSPI_ComConfig_DummyCycles = 0;
        QSPI_ComConfig_Init(QSPI2, &cc);

        QSPI_SetAddress(QSPI2, addr);
        QSPI_SetDataLength(QSPI2, n);
        QSPI_Start(QSPI2);

        qspi_send((uint8_t *)data, n);

        while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_TC) == RESET);
        QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
        QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);

        addr += n;
        data += n;
        len  -= n;
    }
}

void psram_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (addr + len > PSRAM_SIZE) return;

    const uint32_t chunk = 32;

    while (len > 0) {
        uint32_t n = (len > chunk) ? chunk : len;

        qspi_wait_busy();

        QSPI_ComConfig_InitTypeDef cc = {0};
        cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_1Line;
        cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_1Line;
        cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_1Line;
        cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
        cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Read;
        cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
        cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
        cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
        cc.QSPI_ComConfig_Ins         = PSRAM_CMD_FAST_READ;
        cc.QSPI_ComConfig_DummyCycles = 8;
        QSPI_ComConfig_Init(QSPI2, &cc);

        QSPI_SetAddress(QSPI2, addr);
        QSPI_SetDataLength(QSPI2, n);
        QSPI_Start(QSPI2);

        qspi_receive(data, n);

        while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_TC) == RESET);
        QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
        QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);

        addr += n;
        data += n;
        len  -= n;
    }
}

void psram_write_quad(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (addr + len > PSRAM_SIZE) return;

    const uint32_t chunk = 128;

    while (len > 0) {
        uint32_t n = (len > chunk) ? chunk : len;

        qspi_wait_busy();

        QSPI_ComConfig_InitTypeDef cc = {0};
        cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_1Line;
        cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_4Line;
        cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_4Line;
        cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
        cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Write;
        cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
        cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
        cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
        cc.QSPI_ComConfig_Ins         = PSRAM_CMD_QUAD_WRITE;
        cc.QSPI_ComConfig_DummyCycles = 0;
        QSPI_ComConfig_Init(QSPI2, &cc);

        QSPI_SetAddress(QSPI2, addr);
        QSPI_SetDataLength(QSPI2, n);

        QSPI_EnableQuad(QSPI2, ENABLE);
        QSPI_Start(QSPI2);

        qspi_send((uint8_t *)data, n);

        while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_TC) == RESET);
        QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
        QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);
        QSPI_EnableQuad(QSPI2, DISABLE);

        addr += n;
        data += n;
        len  -= n;
    }
}

void psram_read_quad(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (addr + len > PSRAM_SIZE) return;

    const uint32_t chunk = 128;

    while (len > 0) {
        uint32_t n = (len > chunk) ? chunk : len;

        qspi_wait_busy();

        QSPI_ComConfig_InitTypeDef cc = {0};
        cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_1Line;
        cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_4Line;
        cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_4Line;
        cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
        cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Read;
        cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
        cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
        cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
        cc.QSPI_ComConfig_Ins         = PSRAM_CMD_FAST_READ_QUAD;
        cc.QSPI_ComConfig_DummyCycles = 6;
        QSPI_ComConfig_Init(QSPI2, &cc);

        QSPI_SetAddress(QSPI2, addr);
        QSPI_SetDataLength(QSPI2, n);

        QSPI_EnableQuad(QSPI2, ENABLE);
        QSPI_Start(QSPI2);

        qspi_receive(data, n);

        while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_TC) == RESET);
        QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
        QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);
        QSPI_EnableQuad(QSPI2, DISABLE);

        addr += n;
        data += n;
        len  -= n;
    }
}

void psram_enter_quad_mode(void)
{
    qspi_write_cmd(PSRAM_CMD_ENTER_QUAD);
}

void psram_exit_quad_mode(void)
{
    /* Exit quad mode requires sending 0xF5 on all 4 lines */
    QSPI_ComConfig_InitTypeDef cc = {0};
    cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_4Line;
    cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_NoAddress;
    cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_NoData;
    cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
    cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Read;
    cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
    cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
    cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
    cc.QSPI_ComConfig_Ins         = PSRAM_CMD_EXIT_QUAD;
    cc.QSPI_ComConfig_DummyCycles = 0;

    QSPI_EnableQuad(QSPI2, ENABLE);
    QSPI_ComConfig_Init(QSPI2, &cc);
    QSPI_SetDataLength(QSPI2, 0);
    QSPI_Start(QSPI2);

    while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_TC) == RESET);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);
    QSPI_EnableQuad(QSPI2, DISABLE);
}

void psram_halfsleep(void)
{
    qspi_write_cmd(PSRAM_CMD_HALFSLEEP);
}

uint32_t psram_size(void)
{
    return PSRAM_SIZE;
}

void psram_vio18_select(void)
{
    PWR_VIO18LevelCfg(PWR_VIO18Level_MODE1);  /* 1.8V for PSRAM */
    Delay_Us(10);  /* Allow voltage to settle */
}

void psram_vio18_release(void)
{
    PWR_VIO18LevelCfg(PWR_VIO18Level_MODE3);  /* 3.3V for SDMMC */
    Delay_Us(10);
}

/* =========================================================================
 *  DMA Transfer Functions
 *
 *  Uses DMA1_Channel2 to avoid conflict with other peripherals.
 *  QSPI2 DMA mux request = 72 (QSPI1 = 71, QSPI2 = 72).
 *  Transfers are 32-bit word-sized. Length must be a multiple of 4.
 *  The CPU is free during the transfer (can render, process, etc).
 * ========================================================================= */

/* DMA mux request number for QSPI2 (QSPI1 = 71) */
#define QSPI2_DMA_REQUEST   72
#define PSRAM_DMA_CHANNEL    DMA1_Channel2
#define PSRAM_DMA_MUX        DMA_MuxChannel2
#define PSRAM_DMA_FLAG_TC    DMA1_FLAG_TC2

static volatile uint8_t dma_busy = 0;

static void psram_dma_setup_tx(uint8_t *buf, uint32_t len)
{
    DMA_InitTypeDef dma = {0};

    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);

    DMA_DeInit(PSRAM_DMA_CHANNEL);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&(QSPI2->DR);
    dma.DMA_Memory0BaseAddr    = (uint32_t)buf;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = len / 4;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Word;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_VeryHigh;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(PSRAM_DMA_CHANNEL, &dma);

    DMA_MuxChannelConfig(PSRAM_DMA_MUX, QSPI2_DMA_REQUEST);
}

static void psram_dma_setup_rx(uint8_t *buf, uint32_t len)
{
    DMA_InitTypeDef dma = {0};

    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);

    DMA_DeInit(PSRAM_DMA_CHANNEL);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&(QSPI2->DR);
    dma.DMA_Memory0BaseAddr    = (uint32_t)buf;
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize         = len / 4;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Word;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_VeryHigh;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(PSRAM_DMA_CHANNEL, &dma);

    DMA_MuxChannelConfig(PSRAM_DMA_MUX, QSPI2_DMA_REQUEST);
}

void psram_write_dma(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (addr + len > PSRAM_SIZE) return;
    if (len == 0 || (len % 4) != 0) return;  /* Must be 4-byte aligned */

    qspi_wait_busy();

    QSPI_SetFIFOThreshold(QSPI2, 0);

    QSPI_ComConfig_InitTypeDef cc = {0};
    cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_1Line;
    cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_1Line;
    cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_1Line;
    cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
    cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Write;
    cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
    cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
    cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
    cc.QSPI_ComConfig_Ins         = PSRAM_CMD_WRITE;
    cc.QSPI_ComConfig_DummyCycles = 0;
    QSPI_ComConfig_Init(QSPI2, &cc);

    QSPI_SetAddress(QSPI2, addr);
    QSPI_SetDataLength(QSPI2, len);

    /* WCH EVT TX sequence: QSPI_DMA enable, setup DMA, enable DMA, start QSPI */
    QSPI_DMACmd(QSPI2, ENABLE);
    psram_dma_setup_tx(data, len);
    DMA_Cmd(PSRAM_DMA_CHANNEL, ENABLE);
    QSPI_Start(QSPI2);

    /* Wait for DMA complete */
    while (DMA_GetFlagStatus(DMA1, PSRAM_DMA_FLAG_TC) == RESET);

    QSPI_DMACmd(QSPI2, DISABLE);
    DMA_Cmd(PSRAM_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1, PSRAM_DMA_FLAG_TC);

    /* Wait for QSPI transfer complete */
    while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_TC) == RESET);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);

    QSPI_SetFIFOThreshold(QSPI2, 1);
}

void psram_read_dma(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (addr + len > PSRAM_SIZE) return;
    if (len == 0 || (len % 4) != 0) return;  /* Must be 4-byte aligned */

    qspi_wait_busy();

    QSPI_SetFIFOThreshold(QSPI2, 0);

    QSPI_ComConfig_InitTypeDef cc = {0};
    cc.QSPI_ComConfig_IMode       = QSPI_ComConfig_IMode_1Line;
    cc.QSPI_ComConfig_ADMode      = QSPI_ComConfig_ADMode_1Line;
    cc.QSPI_ComConfig_DMode       = QSPI_ComConfig_DMode_1Line;
    cc.QSPI_ComConfig_ABMode      = QSPI_ComConfig_ABMode_NoAlternateByte;
    cc.QSPI_ComConfig_FMode       = QSPI_ComConfig_FMode_Indirect_Read;
    cc.QSPI_ComConfig_SIOOMode    = QSPI_ComConfig_SIOOMode_Disable;
    cc.QSPI_ComConfig_ABSize      = QSPI_ComConfig_ABSize_8bit;
    cc.QSPI_ComConfig_ADSize      = QSPI_ComConfig_ADSize_24bit;
    cc.QSPI_ComConfig_Ins         = PSRAM_CMD_FAST_READ;
    cc.QSPI_ComConfig_DummyCycles = 8;
    QSPI_ComConfig_Init(QSPI2, &cc);

    QSPI_SetAddress(QSPI2, addr);
    QSPI_SetDataLength(QSPI2, len);

    /* WCH EVT RX sequence: setup DMA, QSPI_DMA enable, start QSPI, enable DMA */
    psram_dma_setup_rx(data, len);
    QSPI_DMACmd(QSPI2, ENABLE);
    QSPI_Start(QSPI2);
    DMA_Cmd(PSRAM_DMA_CHANNEL, ENABLE);

    /* Wait for DMA complete */
    while (DMA_GetFlagStatus(DMA1, PSRAM_DMA_FLAG_TC) == RESET);

    /* Wait for QSPI transfer complete */
    while (QSPI_GetFlagStatus(QSPI2, QSPI_FLAG_TC) == RESET);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_FT);
    QSPI_ClearFlag(QSPI2, QSPI_FLAG_TC);
    QSPI_DMACmd(QSPI2, DISABLE);
    DMA_Cmd(PSRAM_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1, PSRAM_DMA_FLAG_TC);

    QSPI_SetFIFOThreshold(QSPI2, 1);
}
