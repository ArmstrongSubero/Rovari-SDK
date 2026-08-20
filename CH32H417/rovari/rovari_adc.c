/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_adc.c - ADC implementation (CH32H417)
 *
 * Uses ADC1 in single-conversion software-triggered mode.
 * 12-bit resolution (0-4095), right-aligned.
 *
 * ADC clock: HCLK / PPRE2 / ADCPRE
 *   At HCLK = 150 MHz, PPRE2 /4 = 37.5 MHz, ADCPRE /8 = ~4.7 MHz
 *   This is within the ADC's operating range.
 *
 * Pin-to-channel mapping (same as datasheet):
 *   PA0-PA7  -> ADC_Channel_0  to ADC_Channel_7
 *   PB0-PB1  -> ADC_Channel_8  to ADC_Channel_9
 *   PC0-PC5  -> ADC_Channel_10 to ADC_Channel_15
 *   Channel 16 = internal temperature sensor
 *   Channel 17 = internal Vrefint (~1.2V)
 *
 * Calibration is performed once on first init.
 */

#include "rovari_adc.h"
#include "debug.h"

/* -- Pin-to-channel mapping ----------------------------------------------- */
typedef struct {
    pin_t         pin;
    uint8_t       channel;
    uint32_t      rcc_gpio;
    GPIO_TypeDef* gpio_port;
    uint16_t      gpio_pin;
} AdcPinDef;

static const AdcPinDef adc_pins[] = {
    /* Port A: channels 0-7 */
    { PA0, ADC_Channel_0,  RCC_HB2Periph_GPIOA, GPIOA, GPIO_Pin_0 },
    { PA1, ADC_Channel_1,  RCC_HB2Periph_GPIOA, GPIOA, GPIO_Pin_1 },
    { PA2, ADC_Channel_2,  RCC_HB2Periph_GPIOA, GPIOA, GPIO_Pin_2 },
    { PA3, ADC_Channel_3,  RCC_HB2Periph_GPIOA, GPIOA, GPIO_Pin_3 },
    { PA4, ADC_Channel_4,  RCC_HB2Periph_GPIOA, GPIOA, GPIO_Pin_4 },
    { PA5, ADC_Channel_5,  RCC_HB2Periph_GPIOA, GPIOA, GPIO_Pin_5 },
    { PA6, ADC_Channel_6,  RCC_HB2Periph_GPIOA, GPIOA, GPIO_Pin_6 },
    { PA7, ADC_Channel_7,  RCC_HB2Periph_GPIOA, GPIOA, GPIO_Pin_7 },

    /* Port B: channels 8-9 (VIO18 domain, but ADC input works fine) */
    { PB0, ADC_Channel_8,  RCC_HB2Periph_GPIOB, GPIOB, GPIO_Pin_0 },
    { PB1, ADC_Channel_9,  RCC_HB2Periph_GPIOB, GPIOB, GPIO_Pin_1 },

    /* Port C: channels 10-15 */
    { PC0, ADC_Channel_10, RCC_HB2Periph_GPIOC, GPIOC, GPIO_Pin_0 },
    { PC1, ADC_Channel_11, RCC_HB2Periph_GPIOC, GPIOC, GPIO_Pin_1 },
    { PC2, ADC_Channel_12, RCC_HB2Periph_GPIOC, GPIOC, GPIO_Pin_2 },
    { PC3, ADC_Channel_13, RCC_HB2Periph_GPIOC, GPIOC, GPIO_Pin_3 },
    { PC4, ADC_Channel_14, RCC_HB2Periph_GPIOC, GPIOC, GPIO_Pin_4 },
    { PC5, ADC_Channel_15, RCC_HB2Periph_GPIOC, GPIOC, GPIO_Pin_5 },
};

#define ADC_PIN_COUNT (sizeof(adc_pins) / sizeof(adc_pins[0]))

static const AdcPinDef* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < ADC_PIN_COUNT; i++) {
        if (adc_pins[i].pin == pin) return &adc_pins[i];
    }
    return 0;
}

/* -- State tracking ------------------------------------------------------- */
static uint8_t adc_initialized = 0;
static uint8_t sample_times[18] = {0};      /* per-channel sample time override */
static uint8_t sample_time_set[18] = {0};   /* 1 = user has set a custom time */

#define VREF_VOLTAGE  3.3f
#define ADC_MAX_VALUE 4095

/* -- Internal: one-time ADC1 init and calibration ------------------------- */
static void adc_hw_init(void)
{
    if (adc_initialized) return;

    /* Enable ADC1 clock (HB2 bus) */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_ADC1, ENABLE);

    /* ADC clock source: HCLK with prescaler
     * HCLK = 150 MHz, PPRE2 /4 = 37.5 MHz, ADCPRE /8 = ~4.7 MHz */
    RCC_ADCCLKConfig(RCC_ADCCLKSource_HCLK);
    RCC_ADCHCLKCLKAsSourceConfig(RCC_PPRE2_DIV4, RCC_HCLK_ADCPRE_DIV8);

    /* Configure ADC1: independent, single conversion, software trigger */
    ADC_InitTypeDef adc = {0};
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    /* Enable low power mode (recommended by WCH for single conversions) */
    ADC_LowPowerModeCmd(ADC1, ENABLE);

    /* Enable ADC1 */
    ADC_Cmd(ADC1, ENABLE);

    /* Calibration sequence */
    ADC_BufferCmd(ADC1, DISABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    adc_initialized = 1;
}

/* -- Internal: read one channel ------------------------------------------- */
static uint16_t read_single_channel(uint8_t channel)
{
    uint8_t stime = (channel < 18 && sample_time_set[channel])
                    ? sample_times[channel]
                    : ADC_SampleTime_CyclesMode5;

    ADC_RegularChannelConfig(ADC1, channel, 1, stime);

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

    return ADC_GetConversionValue(ADC1);
}

/* =========================================================================
 *  Public API
 * ========================================================================= */

void adc_init(pin_t pin)
{
    const AdcPinDef* def = find_pin(pin);
    if (!def) return;

    adc_hw_init();

    /* Enable GPIO clock and configure pin as analog input */
    RCC_HB2PeriphClockCmd(def->rcc_gpio, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = def->gpio_pin;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(def->gpio_port, &gpio);
}

void adc_set_sample_time(pin_t pin, uint8_t cycles)
{
    const AdcPinDef* def = find_pin(pin);
    if (!def) return;
    if (def->channel >= 18) return;

    sample_times[def->channel] = cycles;
    sample_time_set[def->channel] = 1;
}

uint16_t analog_read(pin_t pin)
{
    const AdcPinDef* def = find_pin(pin);
    if (!def) return 0;

    return read_single_channel(def->channel);
}

float analog_read_voltage(pin_t pin)
{
    uint16_t raw = analog_read(pin);
    return (float)raw * VREF_VOLTAGE / (float)ADC_MAX_VALUE;
}

void adc_init_internal(void)
{
    adc_hw_init();
    ADC_TempSensorVrefintCmd(ENABLE);
}

float adc_read_temperature(void)
{
    uint16_t raw = read_single_channel(ADC_Channel_TempSensor);

    /* Convert using WCH formula:
     * V_sense = raw * VREF / 4095
     * T = (V25 - V_sense) / Avg_Slope + 25
     * V25 ~ 1.34V, Avg_Slope ~ 4.3 mV/C */
    float v_sense = (float)raw * VREF_VOLTAGE / (float)ADC_MAX_VALUE;
    return (1.34f - v_sense) / 0.0043f + 25.0f;
}

float adc_read_vrefint(void)
{
    uint16_t raw = read_single_channel(ADC_Channel_Vrefint);
    return (float)raw * VREF_VOLTAGE / (float)ADC_MAX_VALUE;
}

uint16_t adc_read_channel(uint8_t channel)
{
    if (channel > 17) return 0;
    return read_single_channel(channel);
}
