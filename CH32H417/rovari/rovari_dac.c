/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_dac.c - DAC implementation (CH32H417)
 *
 * Dual-channel 12-bit DAC:
 *   Channel 1 -> PA4 (3.3V VDDIO)
 *   Channel 2 -> PA5 (3.3V VDDIO)
 *
 * Uses no-trigger mode: writing to the data register updates
 * the output immediately on the next bus clock cycle.
 *
 * DAC is on HB1 bus. GPIO is on HB2 bus.
 */

#include "rovari_dac.h"
#include "debug.h"

#define VREF_VOLTAGE  3.3f
#define DAC_MAX_VALUE 4095

/* -- Channel identification ----------------------------------------------- */
static int pin_to_channel(pin_t pin)
{
    if (pin == PA4) return 1;
    if (pin == PA5) return 2;
    return 0;
}

static uint8_t ch_initialized[3] = {0};

/* =========================================================================
 *  Public API
 * ========================================================================= */

void dac_init(pin_t pin)
{
    int ch = pin_to_channel(pin);
    if (ch == 0) return;

    /* Enable DAC (HB1) and GPIOA (HB2) clocks */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_DAC, ENABLE);
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA, ENABLE);

    /* Configure pin as analog (disconnects digital buffer) */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = (ch == 1) ? GPIO_Pin_4 : GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    /* Configure DAC channel:
     *   - No trigger (output updates immediately on data write)
     *   - No wave generation
     *   - Output buffer disabled (full range for high-Z loads)
     */
    DAC_InitTypeDef dac = {0};
    dac.DAC_Trigger                          = DAC_Trigger_None;
    dac.DAC_WaveGeneration                   = DAC_WaveGeneration_None;
    dac.DAC_LFSRUnmask_TriangleAmplitude     = DAC_LFSRUnmask_Bit0;
    dac.DAC_OutputBuffer                     = DAC_OutputBuffer_Disable;

    if (ch == 1) {
        DAC_Init(DAC_Channel_1, &dac);
        DAC_Cmd(DAC_Channel_1, ENABLE);
        DAC_SetChannel1Data(DAC_Align_12b_R, 0);
    } else {
        DAC_Init(DAC_Channel_2, &dac);
        DAC_Cmd(DAC_Channel_2, ENABLE);
        DAC_SetChannel2Data(DAC_Align_12b_R, 0);
    }

    ch_initialized[ch] = 1;
}

void dac_write(pin_t pin, uint16_t value)
{
    int ch = pin_to_channel(pin);
    if (ch == 0) return;

    if (value > DAC_MAX_VALUE) value = DAC_MAX_VALUE;

    if (ch == 1) {
        DAC_SetChannel1Data(DAC_Align_12b_R, value);
    } else {
        DAC_SetChannel2Data(DAC_Align_12b_R, value);
    }
}

void dac_write_voltage(pin_t pin, float voltage)
{
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > VREF_VOLTAGE) voltage = VREF_VOLTAGE;

    uint16_t value = (uint16_t)(voltage * (float)DAC_MAX_VALUE / VREF_VOLTAGE);
    dac_write(pin, value);
}

void dac_write_pct(pin_t pin, float percent)
{
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    uint16_t value = (uint16_t)(percent * (float)DAC_MAX_VALUE / 100.0f);
    dac_write(pin, value);
}

void dac_buffer(pin_t pin, uint8_t enable)
{
    int ch = pin_to_channel(pin);
    if (ch == 0) return;

    DAC_InitTypeDef dac = {0};
    dac.DAC_Trigger                          = DAC_Trigger_None;
    dac.DAC_WaveGeneration                   = DAC_WaveGeneration_None;
    dac.DAC_LFSRUnmask_TriangleAmplitude     = DAC_LFSRUnmask_Bit0;
    dac.DAC_OutputBuffer                     = enable ? DAC_OutputBuffer_Enable
                                                      : DAC_OutputBuffer_Disable;

    if (ch == 1) {
        DAC_Init(DAC_Channel_1, &dac);
    } else {
        DAC_Init(DAC_Channel_2, &dac);
    }
}

void dac_stop(pin_t pin)
{
    int ch = pin_to_channel(pin);
    if (ch == 0) return;

    if (ch == 1) {
        DAC_Cmd(DAC_Channel_1, DISABLE);
    } else {
        DAC_Cmd(DAC_Channel_2, DISABLE);
    }

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = (ch == 1) ? GPIO_Pin_4 : GPIO_Pin_5;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOA, &gpio);
    GPIO_ResetBits(GPIOA, gpio.GPIO_Pin);

    ch_initialized[ch] = 0;
}
