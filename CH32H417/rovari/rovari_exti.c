/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_exti.c - External interrupt implementation (CH32H417)
 *
 * EXTI architecture:
 *   GPIO pin -> AFIO_EXTICRx (selects which port for each line)
 *           -> EXTI peripheral (edge detect, masking)
 *           -> PFIC (interrupt controller)
 *           -> ISR handler -> user callback
 *
 * There are 16 EXTI lines (0-15), mapped 1:1 to pin numbers.
 * PA0, PB0, PC0, PD0, PE0, PF0 all share EXTI line 0.
 * Only one port can be active per line.
 *
 * IRQ mapping (CH32H417):
 *   EXTI 0-7   -> EXTI7_0_IRQn    (shared handler)
 *   EXTI 8-15  -> EXTI15_8_IRQn   (shared handler)
 */

#include "rovari_exti.h"
#include "rovari_gpio.h"
#include "debug.h"

/* -- Callback table: one slot per EXTI line (0-15) ------------------------ */
static volatile ExtiCallback exti_callbacks[16] = {0};

/* -- Port source lookup for AFIO_EXTICRx ---------------------------------- */
static const uint8_t port_source[] = {
    GPIO_PortSourceGPIOA,  /* 0 */
    GPIO_PortSourceGPIOB,  /* 1 */
    GPIO_PortSourceGPIOC,  /* 2 */
    GPIO_PortSourceGPIOD,  /* 3 */
    GPIO_PortSourceGPIOE,  /* 4 */
    GPIO_PortSourceGPIOF,  /* 5 */
};

/* -- IRQ number lookup per EXTI line -------------------------------------- */
static IRQn_Type get_irqn(uint8_t line)
{
    if (line <= 7)
        return EXTI7_0_IRQn;
    else
        return EXTI15_8_IRQn;
}

/* =========================================================================
 *  Public C API
 * ========================================================================= */

void attach_interrupt(pin_t pin, EdgeMode edge, ExtiCallback callback)
{
    uint8_t port_idx = ROVARI_PORT(pin);
    uint8_t pin_num  = ROVARI_PIN_NUM(pin);

    if (port_idx > 5 || pin_num > 15) return;

    /* Store callback */
    exti_callbacks[pin_num] = callback;

    /* Enable AFIO clock (required for EXTI port mapping) */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);

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

    /* Enable the interrupt in the PFIC */
    IRQn_Type irqn = get_irqn(pin_num);
    NVIC_SetPriority(irqn, 1);
    NVIC_EnableIRQ(irqn);
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

/* =========================================================================
 *  ISR handlers - dispatch to user callbacks
 *
 *  CH32H417 has two shared EXTI handlers:
 *    EXTI7_0_IRQHandler   -> lines 0-7
 *    EXTI15_8_IRQHandler  -> lines 8-15
 * ========================================================================= */

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

void EXTI7_0_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI7_0_IRQHandler(void)
{
    for (uint8_t i = 0; i <= 7; i++) dispatch(i);
}

void EXTI15_8_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI15_8_IRQHandler(void)
{
    for (uint8_t i = 8; i <= 15; i++) dispatch(i);
}
