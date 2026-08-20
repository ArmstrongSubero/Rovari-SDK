/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_adc.c
 * @brief 12-bit ADC implementation for CH32V307 (SEVS-Core, integer API).
 *
 * ADC1 single-conversion software-triggered mode. ADC clock = PCLK2/8.
 * Calibration runs once on first init. Calibration and EOC polling are
 * bounded so the ADC cannot hang the CPU. All conversions are integer
 * (millivolts, tenths of degC); no floating point per SEVS convention.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_adc.h"

/* Bounded poll cap for calibration / end-of-conversion waits. */
#define ADC_TIMEOUT  100000U

/* Temperature sensor constants (CH32V307 datasheet typical):
 *   V25 = 1340 mV, slope = 4.3 mV/degC (43 in 0.1 mV units). */
#define ADC_TEMP_V25_MV     1340
#define ADC_TEMP_SLOPE_MV10 43      /* 4.3 mV/degC expressed in 0.1 mV/degC */
#define ADC_TEMP_CHANNEL    16
#define ADC_VREF_CHANNEL    17
#define ADC_MAX_CHANNEL     17

/* Pin-to-channel mapping */
typedef struct {
    pin_t         pin;
    uint8_t       channel;
    uint32_t      rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t      gpio_pin;
} adc_pin_def_t;

static const adc_pin_def_t adc_pins[] = {
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

#define ADC_PIN_COUNT (sizeof(adc_pins) / sizeof(adc_pins[0]))

/**
 * @brief Find the ADC pin definition for a pin, or NULL if not ADC-capable.
 */
static const adc_pin_def_t* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < ADC_PIN_COUNT; i++) {
        if (adc_pins[i].pin == pin) {
            return &adc_pins[i];
        }
    }
    return NULL;
}

/* State tracking */
static uint8_t s_adc_initialized = 0;
static uint8_t s_sample_times[18] = {0};
static uint8_t s_sample_time_set[18] = {0};

/**
 * @brief Perform one-time ADC1 init and calibration.
 * @req REQ-ROVARI-ADC-0010
 * @req REQ-ROVARI-ADC-0020
 */
static void adc_hw_init(void)
{
    if (s_adc_initialized) {
        return;
    }

    /* Enable ADC1 clock (on APB2) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    /* ADC clock prescaler: PCLK2 / 8 (18 MHz at 144 MHz PCLK2). */
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    ADC_InitTypeDef adc = {0};
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    ADC_Cmd(ADC1, ENABLE);

    /* Calibration sequence (bounded waits). */
    ADC_BufferCmd(ADC1, DISABLE);
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
    SEVS_INVARIANT(s_adc_initialized == 1);
}

/**
 * @brief Convert a single channel and return the 12-bit result.
 * @req REQ-ROVARI-ADC-0011
 * @req REQ-ROVARI-ADC-0020
 */
static uint16_t read_single_channel(uint8_t channel)
{
    SEVS_INVARIANT(channel <= ADC_MAX_CHANNEL);
    uint8_t stime = s_sample_time_set[channel]
                    ? s_sample_times[channel]
                    : ADC_SampleTime_239Cycles5;

    ADC_RegularChannelConfig(ADC1, channel, 1, stime);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    /* Wait for end of conversion (bounded). */
    for (uint32_t i = 0U; i < ADC_TIMEOUT; i++) {
        if (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) {
            return ADC_GetConversionValue(ADC1);
        }
    }
    return 0;  /* Timeout: conversion did not complete */
}

/* -----------------------------------------------------------------------
 *  Public API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize a pin for ADC input.
 * @param[in] pin ADC-capable pin; non-ADC pins are ignored.
 * @req REQ-ROVARI-ADC-0010
 */
void adc_init(pin_t pin)
{
    const adc_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }
    SEVS_INVARIANT(def->channel <= ADC_MAX_CHANNEL);

    adc_hw_init();

    RCC_APB2PeriphClockCmd(def->rcc_gpio, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = def->gpio_pin;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(def->gpio_port, &gpio);
}

/**
 * @brief Read the raw 12-bit conversion for a pin.
 * @param[in] pin ADC-capable pin.
 * @return 0-4095, or 0 for a non-ADC pin.
 * @req REQ-ROVARI-ADC-0011
 */
uint16_t analog_read(pin_t pin)
{
    const adc_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return 0;
    }
    return read_single_channel(def->channel);
}

/**
 * @brief Read a pin's voltage in millivolts.
 * @param[in] pin ADC-capable pin.
 * @return Voltage in millivolts (0-3300).
 * @req REQ-ROVARI-ADC-0012
 */
uint16_t analog_read_mv(pin_t pin)
{
    uint16_t raw = analog_read(pin);
    uint32_t mv = ((uint32_t)raw * ADC_VREF_MV) / ADC_MAX_VALUE;
    SEVS_INVARIANT(mv <= ADC_VREF_MV);
    return (uint16_t)mv;
}

/**
 * @brief Override the per-channel ADC sample time for a pin.
 * @param[in] pin    ADC-capable pin.
 * @param[in] cycles ADC_SampleTime_xxx value.
 * @req REQ-ROVARI-ADC-0013
 */
void adc_set_sample_time(pin_t pin, uint8_t cycles)
{
    const adc_pin_def_t* def = find_pin(pin);
    if (def == NULL) {
        return;
    }
    SEVS_INVARIANT(def->channel < 18U);
    s_sample_times[def->channel] = cycles;
    s_sample_time_set[def->channel] = 1;
}

/**
 * @brief Enable the internal temperature sensor and reference channels.
 * @req REQ-ROVARI-ADC-0014
 */
void adc_init_internal(void)
{
    adc_hw_init();
    ADC_TempSensorVrefintCmd(ENABLE);
}

/**
 * @brief Read the die temperature in tenths of a degree Celsius.
 *
 * Integer conversion: v_sense_mv = raw * VREF / 4095;
 * t10 = (V25 - v_sense) * 10 / slope + 250, all in integer mV/0.1mV units.
 *
 * @return Temperature in 0.1 degC units.
 * @req REQ-ROVARI-ADC-0015
 */
int16_t adc_read_temperature_c10(void)
{
    uint8_t saved_set  = s_sample_time_set[ADC_TEMP_CHANNEL];
    uint8_t saved_time = s_sample_times[ADC_TEMP_CHANNEL];

    s_sample_times[ADC_TEMP_CHANNEL] = ADC_SampleTime_239Cycles5;
    s_sample_time_set[ADC_TEMP_CHANNEL] = 1;

    uint16_t raw = read_single_channel(ADC_Channel_16);

    s_sample_time_set[ADC_TEMP_CHANNEL] = saved_set;
    s_sample_times[ADC_TEMP_CHANNEL] = saved_time;

    /* v_sense in millivolts */
    int32_t v_sense_mv = (int32_t)(((uint32_t)raw * ADC_VREF_MV) / ADC_MAX_VALUE);
    /* (V25 - v_sense) is in mV; multiply by 10 to keep one decimal, divide by
     * slope expressed in 0.1 mV/degC, then add 25.0 degC as 250 (0.1 degC). */
    int32_t t10 = ((ADC_TEMP_V25_MV - v_sense_mv) * 100) / ADC_TEMP_SLOPE_MV10 + 250;
    return (int16_t)t10;
}

/**
 * @brief Read the internal reference voltage in millivolts.
 * @return Vrefint in millivolts.
 * @req REQ-ROVARI-ADC-0016
 */
uint16_t adc_read_vrefint_mv(void)
{
    uint16_t raw = read_single_channel(ADC_Channel_17);
    uint32_t mv = ((uint32_t)raw * ADC_VREF_MV) / ADC_MAX_VALUE;
    return (uint16_t)mv;
}

/**
 * @brief Read an explicit ADC channel number.
 * @param[in] channel Channel 0-17.
 * @return 0-4095, or 0 if out of range.
 * @req REQ-ROVARI-ADC-0017
 */
uint16_t adc_read_channel(uint8_t channel)
{
    if (channel > ADC_MAX_CHANNEL) {
        return 0;
    }
    return read_single_channel(channel);
}
