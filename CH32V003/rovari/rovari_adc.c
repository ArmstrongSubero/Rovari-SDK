/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_adc.c
 * @brief 10-bit ADC implementation for CH32V003 (SEVS-Core, integer API).
 *
 * ADC1 single-conversion software-triggered mode.
 * CH32V003 ADC is 10-bit (0-1023). Calibration uses ADC_Calibration_Vol().
 *
 * Channel-to-pin mapping:
 *   Ch0=PA2, Ch1=PA1, Ch2=PC4, Ch3=PD2,
 *   Ch4=PD3, Ch5=PD5, Ch6=PD6, Ch7=PD4,
 *   Ch8=Vrefint, Ch9=Vcalint
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_adc.h"

#define ADC_TIMEOUT  100000U

#define ADC_VREF_CHANNEL    8
#define ADC_VCAL_CHANNEL    9
#define ADC_MAX_CHANNEL     9

/* Pin-to-channel mapping */
typedef struct {
    pin_t         pin;
    uint8_t       channel;
    uint32_t      rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t      gpio_pin;
} adc_pin_def_t;

static const adc_pin_def_t adc_pins[] = {
    { PA2, ADC_Channel_0, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_2 },
    { PA1, ADC_Channel_1, RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_1 },
    { PC4, ADC_Channel_2, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_4 },
    { PD2, ADC_Channel_3, RCC_APB2Periph_GPIOD, GPIOD, GPIO_Pin_2 },
    { PD3, ADC_Channel_4, RCC_APB2Periph_GPIOD, GPIOD, GPIO_Pin_3 },
    { PD5, ADC_Channel_5, RCC_APB2Periph_GPIOD, GPIOD, GPIO_Pin_5 },
    { PD6, ADC_Channel_6, RCC_APB2Periph_GPIOD, GPIOD, GPIO_Pin_6 },
    { PD4, ADC_Channel_7, RCC_APB2Periph_GPIOD, GPIOD, GPIO_Pin_4 },
};

#define ADC_PIN_COUNT (sizeof(adc_pins) / sizeof(adc_pins[0]))

static const adc_pin_def_t* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < ADC_PIN_COUNT; i++) {
        if (adc_pins[i].pin == pin) {
            return &adc_pins[i];
        }
    }
    return NULL;
}

static uint8_t s_adc_initialized = 0;
static uint8_t s_sample_times[10] = {0};
static uint8_t s_sample_time_set[10] = {0};

/**
 * @brief One-time ADC1 init and calibration.
 * @req REQ-ROVARI-ADC-0010
 */
static void adc_hw_init(void)
{
    if (s_adc_initialized) {
        return;
    }

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    ADC_InitTypeDef adc = {0};
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    /* CH32V003-specific calibration voltage setting */
    ADC_Calibration_Vol(ADC1, ADC_CALVOL_50PERCENT);

    ADC_Cmd(ADC1, ENABLE);

    /* Calibration sequence */
    ADC_ResetCalibration(ADC1);
    for (uint32_t i = 0U; i < ADC_TIMEOUT; i++) {
        if (!ADC_GetResetCalibrationStatus(ADC1)) {
            break;
        }
    }
    ADC_StartCalibration(ADC1);
    for (uint32_t i = 0U; i < ADC_TIMEOUT; i++) {
        if (!ADC_GetCalibrationStatus(ADC1)) {
            break;
        }
    }

    s_adc_initialized = 1;
}

/**
 * @brief Convert a single channel and return the 10-bit result.
 * @req REQ-ROVARI-ADC-0011
 */
static uint16_t read_single_channel(uint8_t channel)
{
    SEVS_INVARIANT(channel <= ADC_MAX_CHANNEL);
    uint8_t stime = s_sample_time_set[channel]
                    ? s_sample_times[channel]
                    : ADC_SampleTime_241Cycles;

    ADC_RegularChannelConfig(ADC1, channel, 1, stime);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    for (uint32_t i = 0U; i < ADC_TIMEOUT; i++) {
        if (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) {
            return ADC_GetConversionValue(ADC1);
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------
 *  Public API
 * ----------------------------------------------------------------------- */

void adc_init(pin_t pin)
{
    const adc_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }

    adc_hw_init();

    RCC_APB2PeriphClockCmd(def->rcc_gpio, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = def->gpio_pin;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(def->gpio_port, &gpio);
}

uint16_t analog_read(pin_t pin)
{
    const adc_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return 0;
    }
    return read_single_channel(def->channel);
}

uint16_t analog_read_mv(pin_t pin)
{
    uint16_t raw = analog_read(pin);
    uint32_t mv = ((uint32_t)raw * ADC_VREF_MV) / ADC_MAX_VALUE;
    return (uint16_t)mv;
}

void adc_set_sample_time(pin_t pin, uint8_t cycles)
{
    const adc_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }
    SEVS_INVARIANT(def->channel < 10U);
    s_sample_times[def->channel] = cycles;
    s_sample_time_set[def->channel] = 1;
}

void adc_init_internal(void)
{
    /* CH32V003: Vrefint (ch8) and Vcalint (ch9) are always available.
     * No enable call needed (no ADC_TempSensorVrefintCmd on this chip). */
    adc_hw_init();
}

uint16_t adc_read_vrefint_mv(void)
{
    uint16_t raw = read_single_channel(ADC_Channel_Vrefint);
    uint32_t mv = ((uint32_t)raw * ADC_VREF_MV) / ADC_MAX_VALUE;
    return (uint16_t)mv;
}

uint16_t adc_read_channel(uint8_t channel)
{
    if (channel > ADC_MAX_CHANNEL) {
        return 0;
    }
    return read_single_channel(channel);
}
