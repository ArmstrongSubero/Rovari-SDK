/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 */

/**
 * @file rovari_comparator.c
 * @brief Analog comparator (OPA) for CH32V003.
 *
 * The CH32V003 has one OPA that can be used as a comparator.
 * Fixed pins: PA2 = V+ (non-inverting), PA1 = V- (inverting).
 * Output is readable on PD4.
 *
 * @req REQ-ROVARI-COMP-0010
 */

#include <stdint.h>
#include "debug.h"
#include "rovari_comparator.h"
#include "rovari_gpio.h"

/**
 * @brief Initialize the OPA as a comparator.
 * @req REQ-ROVARI-COMP-0010
 */
void comparator_init(void)
{
    /* Enable GPIOA clock for analog inputs */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA1 and PA2 as analog inputs */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin  = GPIO_Pin_1 | GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &gpio);

    /* PD4 as input to read comparator output */
    pin_mode(PD4, Input);

    /* Enable OPA: PA2 = V+ (PSEL=0), PA1 = V- (NSEL=0) */
    EXTEN->EXTEN_CTR |= EXTEN_OPA_EN;
    EXTEN->EXTEN_CTR &= ~EXTEN_OPA_PSEL;
    EXTEN->EXTEN_CTR &= ~EXTEN_OPA_NSEL;
}

/**
 * @brief Read the comparator output.
 * @return 1 if PA2 > PA1, 0 otherwise.
 * @req REQ-ROVARI-COMP-0011
 */
uint8_t comparator_read(void)
{
    return digital_read(PD4);
}
