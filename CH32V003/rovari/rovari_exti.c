/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */
/**
 * @file rovari_exti.c
 * @brief External interrupts for CH32V003.
 * CH32V003 has a single combined EXTI7_0_IRQHandler for lines 0-7.
 */
#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_exti.h"

static ExtiCallback s_callbacks[8] = {0};

void __attribute__((interrupt("machine"))) EXTI7_0_IRQHandler(void)
{
    for (uint8_t line = 0; line < 8; line++) {
        uint32_t mask = (1U << line);
        if (EXTI_GetITStatus(mask) != RESET) {
            EXTI_ClearITPendingBit(mask);
            if (s_callbacks[line] != NULL) {
                s_callbacks[line]();
            }
        }
    }
}

void attach_interrupt(pin_t pin, EdgeMode edge, ExtiCallback callback)
{
    uint8_t pin_num = ROVARI_PIN_NUM(pin);
    if (pin_num > 7) return;

    /* Enable AFIO and the GPIO port clock */
    uint8_t port_idx = ROVARI_PORT(pin);
    uint32_t port_rcc;
    uint8_t port_src;
    switch (port_idx) {
        case 0: port_rcc = RCC_APB2Periph_GPIOA; port_src = GPIO_PortSourceGPIOA; break;
        case 2: port_rcc = RCC_APB2Periph_GPIOC; port_src = GPIO_PortSourceGPIOC; break;
        case 3: port_rcc = RCC_APB2Periph_GPIOD; port_src = GPIO_PortSourceGPIOD; break;
        default: return;
    }
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | port_rcc, ENABLE);

    /* Map port to EXTI line */
    GPIO_EXTILineConfig(port_src, pin_num);

    /* Configure edge trigger */
    EXTI_InitTypeDef exti = {0};
    exti.EXTI_Line    = (1U << pin_num);
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_LineCmd = ENABLE;
    switch (edge) {
        case Rising:  exti.EXTI_Trigger = EXTI_Trigger_Rising;         break;
        case Falling: exti.EXTI_Trigger = EXTI_Trigger_Falling;        break;
        default:      exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling; break;
    }
    EXTI_Init(&exti);

    /* Register callback before enabling, clear any stale pending flag */
    s_callbacks[pin_num] = callback;
    EXTI_ClearITPendingBit(1U << pin_num);
    NVIC_EnableIRQ(EXTI7_0_IRQn);
}

void detach_interrupt(pin_t pin)
{
    uint8_t pin_num = ROVARI_PIN_NUM(pin);
    if (pin_num > 7) return;
    EXTI_InitTypeDef exti = {0};
    exti.EXTI_Line    = (1U << pin_num);
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_LineCmd = DISABLE;
    EXTI_Init(&exti);
    s_callbacks[pin_num] = NULL;
}

uint32_t interrupts_disable(void)
{
    uint32_t state;
    __asm volatile("csrr %0, mstatus" : "=r"(state));
    __asm volatile("csrc mstatus, %0" :: "r"(0x88));
    return state;
}

void interrupts_restore(uint32_t state)
{
    __asm volatile("csrw mstatus, %0" :: "r"(state));
}
