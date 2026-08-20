/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */
/**
 * @file rovari_capture.c
 * @brief Input capture for CH32V003 (TIM1 only).
 * Only TIM1 CH1 (PD2) is supported for capture in the default mapping.
 */
#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_capture.h"
#include "rovari_gpio.h"

static CaptureCallback s_capture_cb = NULL;

void __attribute__((interrupt("machine"))) TIM1_CC_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_CC1) != RESET) {
        TIM_ClearITPendingBit(TIM1, TIM_IT_CC1);
        if (s_capture_cb != NULL) {
            s_capture_cb(TIM_GetCapture1(TIM1));
        }
    }
}

void capture_init(pin_t pin, uint16_t prescaler, CaptureCallback callback)
{
    /* Only TIM1 CH1 = PD2 supported */
    if (pin != PD2) return;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 | RCC_APB2Periph_GPIOD, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOD, &gpio);

    TIM_TimeBaseInitTypeDef tb = {0};
    tb.TIM_Prescaler = prescaler;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = 0xFFFF;
    tb.TIM_ClockDivision = 0;
    tb.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tb);

    TIM_ICInitTypeDef ic = {0};
    ic.TIM_Channel     = TIM_Channel_1;
    ic.TIM_ICPolarity  = TIM_ICPolarity_Rising;
    ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
    ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    ic.TIM_ICFilter    = 0x00;
    TIM_ICInit(TIM1, &ic);

    TIM1->INTFR = 0;
    TIM_ITConfig(TIM1, TIM_IT_CC1, ENABLE);
    s_capture_cb = callback;

    NVIC_EnableIRQ(TIM1_CC_IRQn);
    TIM_Cmd(TIM1, ENABLE);
}

void capture_stop(pin_t pin)
{
    if (pin != PD2) return;
    TIM_Cmd(TIM1, DISABLE);
    TIM_ITConfig(TIM1, TIM_IT_CC1, DISABLE);
    s_capture_cb = NULL;
}
