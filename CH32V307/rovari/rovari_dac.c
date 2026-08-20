/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_dac.c
 * @brief 12-bit dual-channel DAC implementation for CH32V307.
 *
 * Channel 1 -> PA4, Channel 2 -> PA5, shared DAC peripheral on APB1.
 * Software-trigger mode: write DHR12Rx then assert the software trigger;
 * the DAC loads the value into DOR on the next APB1 cycle.
 *
 * All conversions are integer (millivolts, whole percent); no floating
 * point is used, per the SEVS no-float convention for driver code.
 */

#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_dac.h"

/**
 * @brief Map a pin to its DAC channel number.
 * @return 1 for PA4, 2 for PA5, 0 if the pin is not a DAC output.
 */
static int pin_to_channel(pin_t pin)
{
    if (pin == PA4) {
        return 1;
    }
    if (pin == PA5) {
        return 2;
    }
    return 0;  /* Invalid */
}

/* Track init state per channel (index 1 and 2 used). */
static uint8_t s_ch_initialized[3] = {0};

/* -----------------------------------------------------------------------
 *  Public API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialize a DAC output channel.
 * @param[in] pin PA4 or PA5; other pins are ignored.
 * @req REQ-ROVARI-DAC-0010
 * @req REQ-ROVARI-DAC-0020
 */
void dac_init(pin_t pin)
{
    int ch = pin_to_channel(pin);
    if (ch == 0) {
        return;
    }
    SEVS_INVARIANT(ch == 1 || ch == 2);

    /* Enable DAC and GPIOA clocks */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* Configure pin as analog (disconnects digital buffer) */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = (ch == 1) ? GPIO_Pin_4 : GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    /* Software trigger, output buffer disabled, no wave generation. */
    DAC_InitTypeDef dac = {0};
    dac.DAC_Trigger        = DAC_Trigger_Software;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_OutputBuffer   = DAC_OutputBuffer_Disable;

    if (ch == 1) {
        DAC_Init(DAC_Channel_1, &dac);
        DAC_Cmd(DAC_Channel_1, ENABLE);
        DAC_SetChannel1Data(DAC_Align_12b_R, 0);
        DAC_SoftwareTriggerCmd(DAC_Channel_1, ENABLE);
    } else {
        DAC_Init(DAC_Channel_2, &dac);
        DAC_Cmd(DAC_Channel_2, ENABLE);
        DAC_SetChannel2Data(DAC_Align_12b_R, 0);
        DAC_SoftwareTriggerCmd(DAC_Channel_2, ENABLE);
    }

    s_ch_initialized[ch] = 1;
}

/**
 * @brief Set DAC output using a 12-bit value (0-4095).
 * @param[in] pin   PA4 or PA5.
 * @param[in] value 12-bit value; values above 4095 are clamped.
 * @req REQ-ROVARI-DAC-0011
 * @req REQ-ROVARI-DAC-0020
 */
void dac_write(pin_t pin, uint16_t value)
{
    int ch = pin_to_channel(pin);
    if (ch == 0) {
        return;
    }
    SEVS_INVARIANT(ch == 1 || ch == 2);

    if (value > DAC_MAX_VALUE) {
        value = (uint16_t)DAC_MAX_VALUE;
    }

    if (ch == 1) {
        DAC_SetChannel1Data(DAC_Align_12b_R, value);
        DAC_SoftwareTriggerCmd(DAC_Channel_1, ENABLE);
    } else {
        DAC_SetChannel2Data(DAC_Align_12b_R, value);
        DAC_SoftwareTriggerCmd(DAC_Channel_2, ENABLE);
    }
}

/**
 * @brief Set DAC output to a target voltage in millivolts.
 *
 * Integer conversion value = mv * DAC_MAX_VALUE / DAC_VREF_MV. The
 * multiply is widened to 32-bit to avoid overflow (3300 * 4095 fits).
 *
 * @param[in] pin PA4 or PA5.
 * @param[in] mv  Millivolts (0-3300); above 3300 is clamped to full scale.
 * @req REQ-ROVARI-DAC-0012
 */
void dac_write_mv(pin_t pin, uint16_t mv)
{
    if (mv > DAC_VREF_MV) {
        mv = (uint16_t)DAC_VREF_MV;
    }
    uint32_t value = ((uint32_t)mv * DAC_MAX_VALUE) / DAC_VREF_MV;
    SEVS_INVARIANT(value <= DAC_MAX_VALUE);
    dac_write(pin, (uint16_t)value);
}

/**
 * @brief Set DAC output as an integer percentage of full scale.
 * @param[in] pin     PA4 or PA5.
 * @param[in] percent 0-100; above 100 is clamped.
 * @req REQ-ROVARI-DAC-0013
 */
void dac_write_pct(pin_t pin, uint8_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    uint32_t value = ((uint32_t)percent * DAC_MAX_VALUE) / 100U;
    SEVS_INVARIANT(value <= DAC_MAX_VALUE);
    dac_write(pin, (uint16_t)value);
}

/**
 * @brief Enable or disable the channel output buffer.
 * @param[in] pin    PA4 or PA5.
 * @param[in] enable Non-zero enables the buffer; zero disables it.
 * @req REQ-ROVARI-DAC-0014
 * @req REQ-ROVARI-DAC-0020
 */
void dac_buffer(pin_t pin, uint8_t enable)
{
    int ch = pin_to_channel(pin);
    if (ch == 0) {
        return;
    }
    SEVS_INVARIANT(ch == 1 || ch == 2);

    DAC_InitTypeDef dac = {0};
    dac.DAC_Trigger        = DAC_Trigger_Software;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_OutputBuffer   = enable ? DAC_OutputBuffer_Enable
                                    : DAC_OutputBuffer_Disable;

    if (ch == 1) {
        DAC_Init(DAC_Channel_1, &dac);
    } else {
        DAC_Init(DAC_Channel_2, &dac);
    }
}

/**
 * @brief Stop the DAC channel and return the pin to GPIO output low.
 * @param[in] pin PA4 or PA5.
 * @req REQ-ROVARI-DAC-0015
 * @req REQ-ROVARI-DAC-0020
 */
void dac_stop(pin_t pin)
{
    int ch = pin_to_channel(pin);
    if (ch == 0) {
        return;
    }
    SEVS_INVARIANT(ch == 1 || ch == 2);

    if (ch == 1) {
        DAC_Cmd(DAC_Channel_1, DISABLE);
    } else {
        DAC_Cmd(DAC_Channel_2, DISABLE);
    }

    /* Reconfigure pin as GPIO output low */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = (ch == 1) ? GPIO_Pin_4 : GPIO_Pin_5;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    GPIO_ResetBits(GPIOA, gpio.GPIO_Pin);

    s_ch_initialized[ch] = 0;
}
