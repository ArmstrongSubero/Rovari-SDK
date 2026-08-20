/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari — rvembedded.com
 *
 * rovari_exti.c — External interrupt implementation
 *
 * EXTI architecture (CH32V307, STM32F1-compatible):
 *
 *   GPIO pin → AFIO_EXTICRx (selects which port for each line)
 *           → EXTI peripheral (edge detect, masking)
 *           → PFIC/NVIC (interrupt controller)
 *           → ISR handler → user callback
 *
 * There are 16 EXTI lines (0–15), mapped 1:1 to pin numbers.
 * PA0, PB0, PC0, PD0, PE0 all share EXTI line 0.
 * Only one port can be active per line.
 *
 * IRQ mapping:
 *   EXTI0       → EXTI0_IRQn
 *   EXTI1       → EXTI1_IRQn
 *   EXTI2       → EXTI2_IRQn
 *   EXTI3       → EXTI3_IRQn
 *   EXTI4       → EXTI4_IRQn
 *   EXTI5–9     → EXTI9_5_IRQn     (shared handler)
 *   EXTI10–15   → EXTI15_10_IRQn   (shared handler)
 */

#include "rovari_exti.h"
#include "rovari_gpio.h"
#include "debug.h"

/* ── Callback table: one slot per EXTI line (0–15) ──────────────────── */
static volatile ExtiCallback exti_callbacks[16] = {0};

/* ── Port source lookup for AFIO_EXTICRx ────────────────────────────── */
static const uint8_t port_source[] = {
    GPIO_PortSourceGPIOA,  /* 0 */
    GPIO_PortSourceGPIOB,  /* 1 */
    GPIO_PortSourceGPIOC,  /* 2 */
    GPIO_PortSourceGPIOD,  /* 3 */
};

/* ── IRQ number lookup per EXTI line ────────────────────────────────── */
static IRQn_Type get_irqn(uint8_t line)
{
    switch (line) {
        case 0:  return EXTI0_IRQn;
        case 1:  return EXTI1_IRQn;
        case 2:  return EXTI2_IRQn;
        case 3:  return EXTI3_IRQn;
        case 4:  return EXTI4_IRQn;
        case 5:  case 6:  case 7:  case 8:  case 9:
            return EXTI9_5_IRQn;
        default:
            return EXTI15_10_IRQn;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public C API
 * ═══════════════════════════════════════════════════════════════════════ */

void attach_interrupt(pin_t pin, EdgeMode edge, ExtiCallback callback)
{
    uint8_t port_idx = ROVARI_PORT(pin);
    uint8_t pin_num  = ROVARI_PIN_NUM(pin);

    if (port_idx > 4 || pin_num > 15) return;

    /* Store callback */
    exti_callbacks[pin_num] = callback;

    /* Enable AFIO clock (required for EXTI port mapping) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* Map this port+pin to the EXTI line */
    GPIO_EXTILineConfig(port_source[port_idx], pin_num);

    /* Configure EXTI line */
    EXTI_InitTypeDef exti = {0};
    exti.EXTI_Line    = (uint32_t)(1 << pin_num);
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_LineCmd = ENABLE;

    switch (edge) {
        case Rising:  exti.EXTI_Trigger = EXTI_Trigger_Rising;         break;
        case Falling: exti.EXTI_Trigger = EXTI_Trigger_Falling;        break;
        case Change:  exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling; break;
    }

    EXTI_Init(&exti);

    /* Enable the interrupt in the NVIC/PFIC */
    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel                   = get_irqn(pin_num);
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 1;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

void detach_interrupt(pin_t pin)
{
    uint8_t pin_num = ROVARI_PIN_NUM(pin);
    if (pin_num > 15) return;

    /* Disable the EXTI line */
    EXTI_InitTypeDef exti = {0};
    exti.EXTI_Line    = (uint32_t)(1 << pin_num);
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_LineCmd = DISABLE;
    EXTI_Init(&exti);

    /* Clear callback */
    exti_callbacks[pin_num] = 0;
}

uint32_t interrupts_disable(void)
{
    uint32_t state;
    __asm volatile ("csrr %0, mstatus" : "=r"(state));
    __asm volatile ("csrc mstatus, 8");  /* Clear MIE (bit 3) */
    return state;
}

void interrupts_restore(uint32_t state)
{
    __asm volatile ("csrw mstatus, %0" :: "r"(state));
}

void interrupts_enable(void)
{
    __asm volatile ("csrs mstatus, 8");  /* Set MIE (bit 3) */
}

/* ═══════════════════════════════════════════════════════════════════════
 *  ISR handlers — dispatch to user callbacks
 *
 *  These must use the WCH-specific interrupt attribute.
 *  EXTI0–4 have dedicated handlers; 5–9 and 10–15 share handlers.
 * ═══════════════════════════════════════════════════════════════════════ */

/* Helper: check and dispatch for a given EXTI line */
static inline void dispatch(uint8_t line)
{
    uint32_t mask = (uint32_t)(1 << line);
    if (EXTI_GetITStatus(mask) != RESET) {
        EXTI_ClearITPendingBit(mask);
        if (exti_callbacks[line]) {
            exti_callbacks[line]();
        }
    }
}

void EXTI0_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI0_IRQHandler(void) { dispatch(0); }

void EXTI1_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI1_IRQHandler(void) { dispatch(1); }

void EXTI2_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI2_IRQHandler(void) { dispatch(2); }

void EXTI3_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI3_IRQHandler(void) { dispatch(3); }

void EXTI4_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI4_IRQHandler(void) { dispatch(4); }

void EXTI9_5_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI9_5_IRQHandler(void)
{
    for (uint8_t i = 5; i <= 9; i++) dispatch(i);
}

void EXTI15_10_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI15_10_IRQHandler(void)
{
    for (uint8_t i = 10; i <= 15; i++) dispatch(i);
}
