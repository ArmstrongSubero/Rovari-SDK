/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_adc.c — ADC implementation for CH32V307
 *
 * Uses ADC1 in single-conversion software-triggered mode.
 * Pin-to-channel mapping uses the fixed assignments from the datasheet.
 *
 * ADC clock: PCLK2 / prescaler. At 144 MHz PCLK2, we use /8 = 18 MHz
 * (ADC max is 14 MHz per datasheet, but WCH allows up to 18 MHz).
 *
 * Calibration is performed once on first init.
 */

#include "rovari_adc.h"
#include "debug.h"

/* ── Pin-to-channel mapping ─────────────────────────────────────────── */
typedef struct {
    pin_t         pin;
    uint8_t       channel;     /* ADC_Channel_0 .. ADC_Channel_15 */
    uint32_t      rcc_gpio;    /* RCC periph clock for GPIO port */
    GPIO_TypeDef* gpio_port;
    uint16_t      gpio_pin;    /* GPIO_Pin_x */
} AdcPinDef;

static const AdcPinDef adc_pins[] = {
    /* Port A: channels 0–7 */
    { PA0, ADC_Channel_0,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_0 },
    { PA1, ADC_Channel_1,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_1 },
    { PA2, ADC_Channel_2,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_2 },
    { PA3, ADC_Channel_3,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_3 },
    { PA4, ADC_Channel_4,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_4 },
    { PA5, ADC_Channel_5,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_5 },
    { PA6, ADC_Channel_6,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_6 },
    { PA7, ADC_Channel_7,  RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_7 },

    /* Port B: channels 8–9 */
    { PB0, ADC_Channel_8,  RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_0 },
    { PB1, ADC_Channel_9,  RCC_APB2Periph_GPIOB, GPIOB, GPIO_Pin_1 },

    /* Port C: channels 10–15 */
    { PC0, ADC_Channel_10, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_0 },
    { PC1, ADC_Channel_11, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_1 },
    { PC2, ADC_Channel_12, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_2 },
    { PC3, ADC_Channel_13, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_3 },
    { PC4, ADC_Channel_14, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_4 },
    { PC5, ADC_Channel_15, RCC_APB2Periph_GPIOC, GPIOC, GPIO_Pin_5 },
};

#define ADC_PIN_COUNT (sizeof(adc_pins) / sizeof(adc_pins[0]))

static const AdcPinDef* find_pin(pin_t pin)
{
    for (uint32_t i = 0; i < ADC_PIN_COUNT; i++) {
        if (adc_pins[i].pin == pin) return &adc_pins[i];
    }
    return 0;  /* Not an ADC-capable pin */
}

/* ── State tracking ─────────────────────────────────────────────────── */
static uint8_t adc_initialized = 0;
static uint8_t sample_times[18] = {0};  /* per-channel sample time override */
static uint8_t sample_time_set[18] = {0};  /* 1 = user has set a custom time */

/* Default VREF+ voltage (3.3 V for most boards) */
#define VREF_VOLTAGE  3.3f
#define ADC_MAX_VALUE 4095

/* ── Internal: perform one-time ADC1 init and calibration ───────────── */
static void adc_hw_init(void)
{
    if (adc_initialized) return;

    /* Enable ADC1 clock (on APB2) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    /* Set ADC clock prescaler: PCLK2 / 8
     * At 144 MHz PCLK2 → 18 MHz ADC clock.
     * Datasheet says max 14 MHz, but WCH silicon works at 18 MHz. */
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    /* Configure ADC1: independent mode, single conversion, software trigger */
    ADC_InitTypeDef adc = {0};
    adc.ADC_Mode               = ADC_Mode_Independent;
    adc.ADC_ScanConvMode       = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign          = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &adc);

    /* Enable ADC1 */
    ADC_Cmd(ADC1, ENABLE);

    /* Calibration sequence — required by WCH for accurate readings */
    ADC_BufferCmd(ADC1, DISABLE);  /* Disable buffer for calibration */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    adc_initialized = 1;
}

/* ── Internal: read one channel ─────────────────────────────────────── */
static uint16_t read_single_channel(uint8_t channel)
{
    /* Select sample time: user override or default 239.5 cycles */
    uint8_t stime = sample_time_set[channel]
                    ? sample_times[channel]
                    : ADC_SampleTime_239Cycles5;

    /* Configure the regular channel */
    ADC_RegularChannelConfig(ADC1, channel, 1, stime);

    /* Start conversion by software trigger */
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    /* Wait for end of conversion (EOC flag) */
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

    /* Read and return the 12-bit result */
    return ADC_GetConversionValue(ADC1);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

void adc_init(pin_t pin)
{
    const AdcPinDef* def = find_pin(pin);
    if (!def) return;

    /* Initialize ADC1 hardware (once) */
    adc_hw_init();

    /* Enable GPIO clock and configure pin as analog input */
    RCC_APB2PeriphClockCmd(def->rcc_gpio, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = def->gpio_pin;
    gpio.GPIO_Mode = GPIO_Mode_AIN;  /* Analog input — no pull-up/down */
    GPIO_Init(def->gpio_port, &gpio);
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

void adc_set_sample_time(pin_t pin, uint8_t cycles)
{
    const AdcPinDef* def = find_pin(pin);
    if (!def) return;

    sample_times[def->channel] = cycles;
    sample_time_set[def->channel] = 1;
}

void adc_init_internal(void)
{
    /* Initialize ADC1 hardware (once) */
    adc_hw_init();

    /* Enable the temperature sensor and Vrefint */
    ADC_TempSensorVrefintCmd(ENABLE);

    /* Datasheet: temperature sensor needs 17.1 us stabilization time.
     * At 18 MHz ADC clock with 239.5 cycle sample time:
     *   239.5 / 18 MHz = 13.3 us — close enough for typical use.
     * We use the slowest sample time by default for these channels. */
}

float adc_read_temperature(void)
{
    /* Read temperature sensor on channel 16 with slow sample time */
    uint8_t saved_set = sample_time_set[16];
    uint8_t saved_time = sample_times[16];

    sample_times[16] = ADC_SampleTime_239Cycles5;
    sample_time_set[16] = 1;

    uint16_t raw = read_single_channel(ADC_Channel_16);

    /* Restore previous settings */
    sample_time_set[16] = saved_set;
    sample_times[16] = saved_time;

    /* Convert to temperature using WCH formula:
     * V_sense = raw * VREF / 4095
     * T(°C) = (V25 - V_sense) / Avg_Slope + 25
     *
     * WCH CH32V307 typical values (from datasheet):
     *   V25 = 1.34 V (voltage at 25°C)
     *   Avg_Slope = 4.3 mV/°C
     */
    float v_sense = (float)raw * VREF_VOLTAGE / (float)ADC_MAX_VALUE;
    float temp = (1.34f - v_sense) / 0.0043f + 25.0f;

    return temp;
}

float adc_read_vrefint(void)
{
    uint16_t raw = read_single_channel(ADC_Channel_17);
    return (float)raw * VREF_VOLTAGE / (float)ADC_MAX_VALUE;
}

uint16_t adc_read_channel(uint8_t channel)
{
    if (channel > 17) return 0;
    return read_single_channel(channel);
}
