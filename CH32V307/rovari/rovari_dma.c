/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_dma.c
 * @brief DMA driver for CH32V307: memcpy, ADC scan, DAC waveform, SPI TX.
 *
 * Channel assignments: mem2mem DMA2_Ch5, ADC1 DMA1_Ch1, DAC CH1 DMA2_Ch3
 * (TIM6 trigger), SPI1_TX DMA1_Ch3, SPI2_TX DMA1_Ch5. TIM7 is reserved for
 * the system tick, so DAC CH2 DMA (PA5) is unsupported. All completion and
 * busy polling is bounded so DMA cannot hang the CPU.
 *
 * The public API of this module is intentionally chip-specific (see the
 * hal_lint ACCEPTED_DRIFT allowlist); it is not expected to match other
 * targets verbatim.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_dma.h"

/* Bounded poll cap for all DMA/peripheral completion waits. */
#define DMA_TIMEOUT  1000000U
#define DMA_TIM_DIV_MAX 65536U

/* ===================================================================
 *  Internal: ADC pin-to-channel mapping
 * =================================================================== */
typedef struct {
    pin_t         pin;
    uint8_t       channel;
    uint32_t      rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t      gpio_pin;
} dma_adc_pin_def_t;

static const dma_adc_pin_def_t adc_pin_table[] = {
    { PA0, ADC_Channel_0,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_0 },
    { PA1, ADC_Channel_1,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_1 },
    { PA2, ADC_Channel_2,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_2 },
    { PA3, ADC_Channel_3,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_3 },
    { PA4, ADC_Channel_4,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_4 },
    { PA5, ADC_Channel_5,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_5 },
    { PA6, ADC_Channel_6,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_6 },
    { PA7, ADC_Channel_7,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_7 },
    { PB0, ADC_Channel_8,  RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_0 },
    { PB1, ADC_Channel_9,  RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_1 },
    { PC0, ADC_Channel_10, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_0 },
    { PC1, ADC_Channel_11, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_1 },
    { PC2, ADC_Channel_12, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_2 },
    { PC3, ADC_Channel_13, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_3 },
    { PC4, ADC_Channel_14, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_4 },
    { PC5, ADC_Channel_15, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_5 },
};

#define ADC_TABLE_SIZE (sizeof(adc_pin_table) / sizeof(adc_pin_table[0]))

/**
 * @brief Find the ADC pin definition for a pin, or NULL if not found.
 */
static const dma_adc_pin_def_t* find_adc_pin(pin_t pin)
{
    for (uint32_t i = 0; i < ADC_TABLE_SIZE; i++) {
        if (adc_pin_table[i].pin == pin) {
            return &adc_pin_table[i];
        }
    }
    return NULL;
}

/**
 * @brief Compute TIM6/period from a sample rate (integer, bounded prescaler).
 */
static void dma_tim6_div(uint32_t tim_clk, uint32_t sample_rate,
                         uint16_t* out_psc, uint16_t* out_arr)
{
    SEVS_REQUIRE_NOT_NULL(out_psc);
    SEVS_REQUIRE_NOT_NULL(out_arr);
    uint32_t div = (sample_rate != 0U) ? (tim_clk / sample_rate) : 1U;
    if (div == 0U) {
        div = 1U;
    }
    if (div <= DMA_TIM_DIV_MAX) {
        *out_psc = 0;
        *out_arr = (uint16_t)(div - 1U);
    } else {
        uint16_t prescaler = (uint16_t)(div / DMA_TIM_DIV_MAX);
        *out_psc = prescaler;
        *out_arr = (uint16_t)((div / (prescaler + 1U)) - 1U);
    }
}

/* ===================================================================
 *  Memory-to-Memory DMA (DMA2_Channel5)
 * =================================================================== */

/**
 * @brief Start a 32-bit memory-to-memory DMA copy.
 * @param[out] dst   Destination (word-aligned).
 * @param[in]  src   Source (word-aligned).
 * @param[in]  count Number of 32-bit words; 0 is a no-op.
 * @req REQ-ROVARI-DMA-0010
 * @req REQ-ROVARI-DMA-0021
 */
void dma_memcpy_start(volatile uint32_t* dst, const volatile uint32_t* src, uint16_t count)
{
    if (count == 0) {
        return;
    }
    SEVS_REQUIRE_NOT_NULL(dst);
    SEVS_REQUIRE_NOT_NULL(src);

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);
    DMA_DeInit(DMA2_Channel5);

    DMA_InitTypeDef dma = {0};
    dma.DMA_PeripheralBaseAddr = (uint32_t)src;
    dma.DMA_MemoryBaseAddr     = (uint32_t)dst;
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize         = count;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Enable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Word;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Enable;

    DMA_Init(DMA2_Channel5, &dma);
    DMA_ClearFlag(DMA2_FLAG_TC5);
    DMA_Cmd(DMA2_Channel5, ENABLE);
}

/**
 * @brief Report whether the mem-to-mem DMA is still running.
 * @return 1 if busy, 0 if complete.
 * @req REQ-ROVARI-DMA-0010
 */
uint8_t dma_memcpy_busy(void)
{
    return (DMA_GetFlagStatus(DMA2_FLAG_TC5) == RESET) ? 1 : 0;
}

/**
 * @brief Wait (bounded) for the mem-to-mem DMA to complete, then disable it.
 * @req REQ-ROVARI-DMA-0010
 * @req REQ-ROVARI-DMA-0020
 */
void dma_memcpy_wait(void)
{
    for (uint32_t i = 0U; i < DMA_TIMEOUT; i++) {
        if (DMA_GetFlagStatus(DMA2_FLAG_TC5) != RESET) {
            break;
        }
    }
    DMA_ClearFlag(DMA2_FLAG_TC5);
    DMA_Cmd(DMA2_Channel5, DISABLE);
}

/**
 * @brief Perform a blocking 32-bit memory-to-memory DMA copy.
 * @param[out] dst   Destination (word-aligned).
 * @param[in]  src   Source (word-aligned).
 * @param[in]  count Number of 32-bit words.
 * @req REQ-ROVARI-DMA-0010
 * @req REQ-ROVARI-DMA-0021
 */
void dma_memcpy(volatile uint32_t* dst, const volatile uint32_t* src, uint16_t count)
{
    SEVS_REQUIRE_NOT_NULL(dst);
    SEVS_REQUIRE_NOT_NULL(src);
    dma_memcpy_start(dst, src, count);
    dma_memcpy_wait();
}

/* ===================================================================
 *  ADC + DMA (continuous scan, DMA1_Channel1)
 * =================================================================== */

static volatile uint16_t* adc_dma_buffer = 0;
static uint8_t            adc_dma_count  = 0;

/**
 * @brief Configure ADC1 multi-channel scan into a DMA ring buffer.
 * @param[in]  pins     Array of ADC-capable pins to scan.
 * @param[in]  num_pins Number of pins (1-16).
 * @param[out] buffer   Destination ring buffer of num_pins half-words.
 * @req REQ-ROVARI-DMA-0011
 * @req REQ-ROVARI-DMA-0020
 * @req REQ-ROVARI-DMA-0021
 */
void dma_adc_init(const pin_t* pins, uint8_t num_pins, volatile uint16_t* buffer)
{
    if (num_pins == 0 || num_pins > 16) {
        return;
    }
    SEVS_REQUIRE_NOT_NULL(pins);
    SEVS_REQUIRE_NOT_NULL(buffer);

    adc_dma_buffer = buffer;
    adc_dma_count  = num_pins;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    for (uint8_t i = 0; i < num_pins; i++) {
        const dma_adc_pin_def_t* def = find_adc_pin(pins[i]);
        if (def == NULL) {
            continue;
        }
        RCC_APB2PeriphClockCmd(def->rcc_gpio, ENABLE);
        GPIO_InitTypeDef gpio = {0};
        gpio.GPIO_Pin  = def->gpio_pin;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(def->gpio_port, &gpio);
    }

    DMA_DeInit(DMA1_Channel1);
    DMA_InitTypeDef dma = {0};
    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    dma.DMA_MemoryBaseAddr     = (uint32_t)buffer;
    dma.DMA_DIR                = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize         = num_pins;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Circular;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma);
    DMA_Cmd(DMA1_Channel1, ENABLE);

    ADC_DeInit(ADC1);
    ADC_InitTypeDef adc = {0};
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = ENABLE;
    adc.ADC_ContinuousConvMode = ENABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = num_pins;
    ADC_Init(ADC1, &adc);

    for (uint8_t i = 0; i < num_pins; i++) {
        const dma_adc_pin_def_t* def = find_adc_pin(pins[i]);
        if (def == NULL) {
            continue;
        }
        ADC_RegularChannelConfig(ADC1, def->channel, i + 1, ADC_SampleTime_239Cycles5);
    }

    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);

    /* Calibrate (bounded waits). */
    ADC_BufferCmd(ADC1, DISABLE);
    ADC_ResetCalibration(ADC1);
    for (uint32_t i = 0U; i < DMA_TIMEOUT; i++) {
        if (!ADC_GetResetCalibrationStatus(ADC1)) {
            break;
        }
    }
    ADC_StartCalibration(ADC1);
    for (uint32_t i = 0U; i < DMA_TIMEOUT; i++) {
        if (!ADC_GetCalibrationStatus(ADC1)) {
            break;
        }
    }
}

/**
 * @brief Start the ADC scan; circular DMA does the rest.
 * @req REQ-ROVARI-DMA-0011
 */
void dma_adc_start(void)
{
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

/**
 * @brief Stop the ADC scan and its DMA channel.
 * @req REQ-ROVARI-DMA-0011
 */
void dma_adc_stop(void)
{
    ADC_Cmd(ADC1, DISABLE);
    DMA_Cmd(DMA1_Channel1, DISABLE);
}

/* ===================================================================
 *  DAC + DMA waveform output (TIM6 -> DAC CH1 -> DMA2_Channel3)
 * =================================================================== */

static uint8_t dac_dma_initialized = 0;

/**
 * @brief Return the TIM6 input clock, applying the APB 2x rule.
 */
static uint32_t get_tim6_clock(void)
{
    RCC_ClocksTypeDef clk;
    RCC_GetClocksFreq(&clk);
    return (clk.PCLK1_Frequency == clk.HCLK_Frequency)
             ? clk.PCLK1_Frequency
             : clk.PCLK1_Frequency * 2U;
}

/**
 * @brief Configure a DAC waveform played from memory at a sample rate.
 * @param[in] pin         Must be PA4 (DAC CH1); other pins ignored.
 * @param[in] waveform    Sample buffer (12-bit right-aligned half-words).
 * @param[in] length      Number of samples; 0 is ignored.
 * @param[in] sample_rate Samples per second; 0 is ignored.
 * @req REQ-ROVARI-DMA-0012
 * @req REQ-ROVARI-DMA-0021
 */
void dma_dac_init(pin_t pin, const uint16_t* waveform, uint16_t length, uint32_t sample_rate)
{
    if (pin != PA4) {
        return;
    }
    if (length == 0 || sample_rate == 0) {
        return;
    }
    SEVS_REQUIRE_NOT_NULL(waveform);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC | RCC_APB1Periph_TIM6, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = GPIO_Pin_4;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    uint32_t tim_clk = get_tim6_clock();
    uint16_t prescaler;
    uint16_t period;
    dma_tim6_div(tim_clk, sample_rate, &prescaler, &period);

    TIM_TimeBaseInitTypeDef tim = {0};
    tim.TIM_Prescaler     = prescaler;
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    tim.TIM_Period        = period;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM6, &tim);
    TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);

    DAC_InitTypeDef dac = {0};
    dac.DAC_Trigger        = DAC_Trigger_T6_TRGO;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_OutputBuffer   = DAC_OutputBuffer_Disable;
    DAC_Init(DAC_Channel_1, &dac);
    DAC_DMACmd(DAC_Channel_1, ENABLE);

    DMA_DeInit(DMA2_Channel3);
    DMA_InitTypeDef dma = {0};
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->R12BDHR1;
    dma.DMA_MemoryBaseAddr     = (uint32_t)waveform;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = length;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode               = DMA_Mode_Circular;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel3, &dma);
    DMA_Cmd(DMA2_Channel3, ENABLE);

    dac_dma_initialized = 1;
}

/**
 * @brief Start DAC waveform playback.
 * @param[in] pin Must be PA4.
 * @req REQ-ROVARI-DMA-0012
 */
void dma_dac_start(pin_t pin)
{
    if (pin != PA4 || !dac_dma_initialized) {
        return;
    }
    DAC_Cmd(DAC_Channel_1, ENABLE);
    TIM_Cmd(TIM6, ENABLE);
}

/**
 * @brief Stop DAC waveform playback.
 * @param[in] pin Must be PA4.
 * @req REQ-ROVARI-DMA-0012
 */
void dma_dac_stop(pin_t pin)
{
    if (pin != PA4) {
        return;
    }
    TIM_Cmd(TIM6, DISABLE);
    DAC_Cmd(DAC_Channel_1, DISABLE);
    DMA_Cmd(DMA2_Channel3, DISABLE);
    dac_dma_initialized = 0;
}

/**
 * @brief Change the DAC waveform sample rate.
 * @param[in] pin         Must be PA4.
 * @param[in] sample_rate New samples per second; 0 is ignored.
 * @req REQ-ROVARI-DMA-0012
 */
void dma_dac_set_rate(pin_t pin, uint32_t sample_rate)
{
    if (pin != PA4 || sample_rate == 0) {
        return;
    }
    uint32_t tim_clk = get_tim6_clock();
    uint16_t prescaler;
    uint16_t period;
    dma_tim6_div(tim_clk, sample_rate, &prescaler, &period);

    TIM6->PSC   = prescaler;
    TIM6->ATRLR = period;
    TIM6->SWEVGR = TIM_PSCReloadMode_Immediate;
}

/* ===================================================================
 *  SPI TX via DMA
 * =================================================================== */

typedef struct {
    SPI_TypeDef*            spi;
    DMA_Channel_TypeDef*    tx_ch;
    uint32_t                tc_flag;
} spi_dma_def_t;

static const spi_dma_def_t spi_dma_defs[] = {
    [0] = { 0,    0,              0              },
    [1] = { SPI1, DMA1_Channel3,  DMA1_FLAG_TC3  },  /* SPI1_TX */
    [2] = { SPI2, DMA1_Channel5,  DMA1_FLAG_TC5  },  /* SPI2_TX */
};

#define SPI_DMA_DEF_COUNT (sizeof(spi_dma_defs) / sizeof(spi_dma_defs[0]))

/**
 * @brief Resolve an SPI instance to its DMA resources, or NULL.
 */
static const spi_dma_def_t* get_spi_dma(SpiInstance inst)
{
    if (inst == 0 || inst >= SPI_DMA_DEF_COUNT) {
        return NULL;
    }
    return &spi_dma_defs[inst];
}

/**
 * @brief Configure the TX DMA channel for an SPI instance.
 * @param[in] inst SPI instance (SPI_1 or SPI_2).
 * @req REQ-ROVARI-DMA-0013
 */
void dma_spi_tx_init(SpiInstance inst)
{
    const spi_dma_def_t* def = get_spi_dma(inst);
    if (def == NULL || def->spi == NULL) {
        return;
    }

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    DMA_DeInit(def->tx_ch);

    DMA_InitTypeDef dma = {0};
    dma.DMA_PeripheralBaseAddr = (uint32_t)&def->spi->DATAR;
    dma.DMA_MemoryBaseAddr     = (uint32_t)0;
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

/**
 * @brief Start a one-shot SPI TX DMA transfer.
 * @param[in] inst SPI instance.
 * @param[in] data Bytes to transmit.
 * @param[in] len  Number of bytes (1-65535); 0 is ignored.
 * @req REQ-ROVARI-DMA-0013
 * @req REQ-ROVARI-DMA-0020
 * @req REQ-ROVARI-DMA-0021
 */
void dma_spi_tx_start(SpiInstance inst, const uint8_t* data, uint16_t len)
{
    const spi_dma_def_t* def = get_spi_dma(inst);
    if (def == NULL || def->spi == NULL || len == 0) {
        return;
    }
    SEVS_REQUIRE_NOT_NULL(data);

    /* Wait (bounded) for any previous SPI activity to finish. */
    for (uint32_t i = 0U; i < DMA_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(def->spi, SPI_I2S_FLAG_BSY) != SET) {
            break;
        }
    }

    DMA_Cmd(def->tx_ch, DISABLE);
    DMA_ClearFlag(def->tc_flag);
    def->tx_ch->MADDR = (uint32_t)data;
    def->tx_ch->CNTR  = len;
    DMA_Cmd(def->tx_ch, ENABLE);
}

/**
 * @brief Report whether an SPI TX DMA transfer is still running.
 * @param[in] inst SPI instance.
 * @return 1 if busy, 0 if complete or invalid instance.
 * @req REQ-ROVARI-DMA-0013
 */
uint8_t dma_spi_tx_busy(SpiInstance inst)
{
    const spi_dma_def_t* def = get_spi_dma(inst);
    if (def == NULL) {
        return 0;
    }
    return (DMA_GetFlagStatus(def->tc_flag) == RESET) ? 1 : 0;
}

/**
 * @brief Wait (bounded) for an SPI TX DMA transfer and the shift register.
 * @param[in] inst SPI instance.
 * @req REQ-ROVARI-DMA-0013
 * @req REQ-ROVARI-DMA-0020
 */
void dma_spi_tx_wait(SpiInstance inst)
{
    const spi_dma_def_t* def = get_spi_dma(inst);
    if (def == NULL || def->spi == NULL) {
        return;
    }

    for (uint32_t i = 0U; i < DMA_TIMEOUT; i++) {
        if (DMA_GetFlagStatus(def->tc_flag) != RESET) {
            break;
        }
    }
    DMA_ClearFlag(def->tc_flag);

    for (uint32_t i = 0U; i < DMA_TIMEOUT; i++) {
        if (SPI_I2S_GetFlagStatus(def->spi, SPI_I2S_FLAG_BSY) != SET) {
            break;
        }
    }
}
