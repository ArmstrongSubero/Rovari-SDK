/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_opa.c - Op-Amp and Comparator implementation (CH32H417)
 *
 * The CH32H417 has 3 op-amps (OPA1/2/3) and 1 comparator (CMP).
 * All share the OPCM peripheral clock on HB2.
 *
 * OPA pins (from datasheet):
 *   OPA1: P0=PB0, P1=PA6, N0=PB1, N1=PA7, OUT0/OUT1=PA5
 *   OPA2: (internal connections only, no external I/O)
 *   OPA3: P0=PC2, P1=PA2, N0=PC3, N1=PA3, OUT0/OUT1=PA4
 *
 * CMP pins:
 *   P0=PB0, P1=PB2, N0=PB1, OUT=PB12 (AF13)
 *
 * OPA modes:
 *   - Unity gain (voltage follower): FB=ON, NSEL=CHN_OFF
 *   - PGA mode: NSEL=CHN_PGA_8x/16x/32x/64x, FB=ON
 *   - External feedback: FB=OFF, connect feedback network to Nx pin
 *
 * High-speed mode (HS=ON) increases slew rate at the cost of higher
 * power consumption.
 */

#include "rovari_opa.h"
#include "debug.h"

/* =========================================================================
 *  OPA Public API
 * ========================================================================= */

void opa_init(uint8_t opa_num, OpaGain gain)
{
    OPA_InitTypeDef opa = {0};
    GPIO_InitTypeDef gpio = {0};

    /* Enable OPCM and GPIO clocks */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_OPCM, ENABLE);

    OPA_Num_TypeDef inst;

    if (opa_num == 1) {
        inst = OPA1;
        /* OPA1: P0=PB0 (input), OUT1=PA5 (output) */
        RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB | RCC_HB2Periph_GPIOA, ENABLE);
        gpio.GPIO_Pin = GPIO_Pin_0;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOB, &gpio);
        /* Configure output pin PA5 as analog */
        gpio.GPIO_Pin = GPIO_Pin_5;
        GPIO_Init(GPIOA, &gpio);
        opa.PSEL = CHP0;
        opa.Mode = OUT_IO_OUT1;  /* OUT1 = PA5 */
    } else if (opa_num == 3) {
        inst = OPA3;
        /* OPA3: P0=PC2 (input), OUT1=PA4 (output) */
        RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC | RCC_HB2Periph_GPIOA, ENABLE);
        gpio.GPIO_Pin = GPIO_Pin_2;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOC, &gpio);
        /* Configure output pin PA4 as analog */
        gpio.GPIO_Pin = GPIO_Pin_4;
        GPIO_Init(GPIOA, &gpio);
        opa.PSEL = CHP0;
        opa.Mode = OUT_IO_OUT1;  /* OUT1 = PA4 */
    } else {
        return;  /* OPA2 has no external pins */
    }

    /* Configure gain */
    switch (gain) {
        case OPA_GAIN_1X:
            /* Unity gain (voltage follower): no negative input, feedback on */
            opa.NSEL = CHN_OFF;
            opa.FB = FB_ON;
            break;
        case OPA_GAIN_8X:
            opa.NSEL = CHN_PGA_8xIN;
            opa.FB = FB_ON;
            break;
        case OPA_GAIN_16X:
            opa.NSEL = CHN_PGA_16xIN;
            opa.FB = FB_ON;
            break;
        case OPA_GAIN_32X:
            opa.NSEL = CHN_PGA_32xIN;
            opa.FB = FB_ON;
            break;
        case OPA_GAIN_64X:
            opa.NSEL = CHN_PGA_64xIN;
            opa.FB = FB_ON;
            break;
    }

    opa.PGADIF = DIF_OFF;
    opa.HS = HS_ON;

    OPA_Init(inst, &opa);
    OPA_Cmd(inst, ENABLE);
}

void opa_stop(uint8_t opa_num)
{
    OPA_Num_TypeDef inst;
    if (opa_num == 1) inst = OPA1;
    else if (opa_num == 3) inst = OPA3;
    else return;

    OPA_Cmd(inst, DISABLE);
}

/* =========================================================================
 *  CMP Public API
 * ========================================================================= */

void cmp_init(CmpPositive pos, CmpNegative neg)
{
    GPIO_InitTypeDef gpio = {0};
    CMP_InitTypeDef cmp = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_OPCM | RCC_HB2Periph_GPIOB | RCC_HB2Periph_AFIO, ENABLE);

    /* Configure input pins as analog */
    /* Positive: PB0 (CMP_P0) or PB2 (CMP_P1) */
    if (pos == CMP_POS_PB0) {
        gpio.GPIO_Pin = GPIO_Pin_0;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOB, &gpio);
    } else {
        gpio.GPIO_Pin = GPIO_Pin_2;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOB, &gpio);
    }

    /* Negative: PB1 (CMP_N0) */
    if (neg == CMP_NEG_PB1) {
        gpio.GPIO_Pin = GPIO_Pin_1;
        gpio.GPIO_Mode = GPIO_Mode_AIN;
        GPIO_Init(GPIOB, &gpio);
    }

    /* Configure CMP output on PB12 (AF13) */
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource12, GPIO_AF13);
    gpio.GPIO_Pin = GPIO_Pin_12;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &gpio);

    cmp.Mode = OUT_TO_IO;
    cmp.PSEL = (pos == CMP_POS_PB0) ? CMP_CHP_0 : CMP_CHP_1;
    cmp.NSEL = CMP_CHN0;
    cmp.VREF = CMP_VREF_OFF;

    OPA_CMP_Init(&cmp);
    OPA_CMP_Cmd(ENABLE);
}

uint8_t cmp_read(void)
{
    return (OPA_CMP_GetOutStatus() == SET) ? 1 : 0;
}

void cmp_stop(void)
{
    OPA_CMP_Cmd(DISABLE);
}
