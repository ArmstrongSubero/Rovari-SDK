/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_exti.c
 * @brief External interrupt (EXTI) implementation for CH32V307.
 *
 * @sevs-callbacks  Implements a function-pointer dispatch table
 *                  (ExtiCallback); JPL Rule 9 function-pointer prohibition
 *                  is suppressed per SEVS Section 2.10.
 *
 * 16 EXTI lines (0-15) map 1:1 to pin numbers; PA0/PB0/PC0/... share line 0.
 * Lines 0-4 have dedicated IRQs; 5-9 and 10-15 share handlers.
 */

#include <stddef.h>
#include <stdint.h>
#include "sevs_runtime.h"
#include "debug.h"
#include "rovari_exti.h"
#include "rovari_gpio.h"

#define EXTI_LINE_COUNT 16
#define EXTI_PORT_MAX   4
#define EXTI_PIN_MAX    15

/* Callback table: one slot per EXTI line (0-15) */
static volatile ExtiCallback s_exti_callbacks[EXTI_LINE_COUNT] = {0};

/* Port source lookup for AFIO_EXTICRx */
static const uint8_t port_source[] = {
    GPIO_PortSourceGPIOA,  /* 0 */
    GPIO_PortSourceGPIOB,  /* 1 */
    GPIO_PortSourceGPIOC,  /* 2 */
    GPIO_PortSourceGPIOD,  /* 3 */
    GPIO_PortSourceGPIOE,  /* 4 */
};

/**
 * @brief Map an EXTI line to its PFIC IRQ number.
 */
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

/* -----------------------------------------------------------------------
 *  Public C API
 * ----------------------------------------------------------------------- */

/**
 * @brief Register an edge-triggered interrupt callback on a pin.
 * @param[in] pin      Encoded pin identifier.
 * @param[in] edge     Trigger edge (Rising, Falling, Change).
 * @param[in] callback Function called from ISR context on the event.
 * @req REQ-ROVARI-EXTI-0010
 * @req REQ-ROVARI-EXTI-0020
 */
void attach_interrupt(pin_t pin, EdgeMode edge, ExtiCallback callback)
{
    uint8_t port_idx = ROVARI_PORT(pin);
    uint8_t pin_num  = ROVARI_PIN_NUM(pin);

    if (port_idx > EXTI_PORT_MAX || pin_num > EXTI_PIN_MAX) {
        return;
    }
    SEVS_INVARIANT(pin_num < EXTI_LINE_COUNT);

    /* Store callback */
    s_exti_callbacks[pin_num] = callback;

    /* Enable AFIO clock (required for EXTI port mapping) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* Map this port+pin to the EXTI line */
    GPIO_EXTILineConfig(port_source[port_idx], pin_num);

    /* Configure EXTI line */
    EXTI_InitTypeDef exti = {0};
    exti.EXTI_Line    = (uint32_t)(1U << pin_num);
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_LineCmd = ENABLE;

    switch (edge) {
        case Rising:  exti.EXTI_Trigger = EXTI_Trigger_Rising;         break;
        case Falling: exti.EXTI_Trigger = EXTI_Trigger_Falling;        break;
        case Change:  exti.EXTI_Trigger = EXTI_Trigger_Rising_Falling; break;
        default:      exti.EXTI_Trigger = EXTI_Trigger_Rising;         break;
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

/**
 * @brief Disable and unregister a pin's EXTI interrupt.
 * @param[in] pin Encoded pin identifier.
 * @req REQ-ROVARI-EXTI-0011
 * @req REQ-ROVARI-EXTI-0020
 */
void detach_interrupt(pin_t pin)
{
    uint8_t pin_num = ROVARI_PIN_NUM(pin);
    if (pin_num > EXTI_PIN_MAX) {
        return;
    }
    SEVS_INVARIANT(pin_num < EXTI_LINE_COUNT);

    /* Disable the EXTI line */
    EXTI_InitTypeDef exti = {0};
    exti.EXTI_Line    = (uint32_t)(1U << pin_num);
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_LineCmd = DISABLE;
    EXTI_Init(&exti);

    /* Clear callback */
    s_exti_callbacks[pin_num] = 0;
}

/**
 * @brief Disable global machine interrupts and return prior mstatus.
 * @return Previous mstatus value for use with interrupts_restore.
 * @req REQ-ROVARI-EXTI-0012
 */
uint32_t interrupts_disable(void)
{
    uint32_t state;
    __asm volatile ("csrr %0, mstatus" : "=r"(state));
    __asm volatile ("csrc mstatus, 8");  /* Clear MIE (bit 3) */
    return state;
}

/**
 * @brief Restore a previously saved interrupt-enable state.
 * @param[in] state Value previously returned by interrupts_disable.
 * @req REQ-ROVARI-EXTI-0012
 */
void interrupts_restore(uint32_t state)
{
    __asm volatile ("csrw mstatus, %0" :: "r"(state));
}

/**
 * @brief Enable global machine interrupts.
 * @req REQ-ROVARI-EXTI-0012
 */
void interrupts_enable(void)
{
    __asm volatile ("csrs mstatus, 8");  /* Set MIE (bit 3) */
}

/* -----------------------------------------------------------------------
 *  ISR handlers: dispatch to user callbacks
 * ----------------------------------------------------------------------- */

/**
 * @brief Clear and dispatch the callback for one EXTI line if pending.
 * @param[in] line EXTI line number (0-15).
 * @req REQ-ROVARI-EXTI-0013
 */
static inline void dispatch(uint8_t line)
{
    SEVS_INVARIANT(line < EXTI_LINE_COUNT);
    uint32_t mask = (uint32_t)(1U << line);
    if (EXTI_GetITStatus(mask) != RESET) {
        EXTI_ClearITPendingBit(mask);
        if (s_exti_callbacks[line] != NULL) {
            s_exti_callbacks[line]();
        }
    }
}

/** @brief EXTI line 0 ISR. @req REQ-ROVARI-EXTI-0013 */
void __attribute__((interrupt("machine"))) EXTI0_IRQHandler(void) { dispatch(0); }
/** @brief EXTI line 1 ISR. @req REQ-ROVARI-EXTI-0013 */
void __attribute__((interrupt("machine"))) EXTI1_IRQHandler(void) { dispatch(1); }
/** @brief EXTI line 2 ISR. @req REQ-ROVARI-EXTI-0013 */
void __attribute__((interrupt("machine"))) EXTI2_IRQHandler(void) { dispatch(2); }
/** @brief EXTI line 3 ISR. @req REQ-ROVARI-EXTI-0013 */
void __attribute__((interrupt("machine"))) EXTI3_IRQHandler(void) { dispatch(3); }
/** @brief EXTI line 4 ISR. @req REQ-ROVARI-EXTI-0013 */
void __attribute__((interrupt("machine"))) EXTI4_IRQHandler(void) { dispatch(4); }

/** @brief Shared ISR for EXTI lines 5-9. @req REQ-ROVARI-EXTI-0013 */
void __attribute__((interrupt("machine"))) EXTI9_5_IRQHandler(void)
{
    for (uint8_t i = 5; i <= 9; i++) {
        dispatch(i);
    }
}

/** @brief Shared ISR for EXTI lines 10-15. @req REQ-ROVARI-EXTI-0013 */
void __attribute__((interrupt("machine"))) EXTI15_10_IRQHandler(void)
{
    for (uint8_t i = 10; i <= 15; i++) {
        dispatch(i);
    }
}
