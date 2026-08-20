/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Rovari - rvembedded.com
 *
 * rovari_rng.c - Hardware Random Number Generator (CH32H417)
 */

#include "rovari_rng.h"
#include "debug.h"

void rng_init(void)
{
    RCC_HBPeriphClockCmd(RCC_HBPeriph_RNG, ENABLE);
    RNG_Cmd(ENABLE);
}

uint32_t rng_read(void)
{
    /* Wait for data-ready flag */
    while (RNG_GetFlagStatus(RNG_FLAG_DRDY) == RESET)
        ;
    return RNG_GetRandomNumber();
}

uint32_t rng_range(uint32_t min, uint32_t max)
{
    if (min >= max)
        return min;

    uint32_t span = max - min + 1;
    return min + (rng_read() % span);
}

void rng_fill(uint8_t *buf, uint16_t len)
{
    uint16_t i = 0;

    while (i < len)
    {
        uint32_t val = rng_read();
        uint8_t remaining = (len - i < 4) ? (uint8_t)(len - i) : 4;
        uint8_t j;

        for (j = 0; j < remaining; j++)
        {
            buf[i++] = (uint8_t)(val & 0xFF);
            val >>= 8;
        }
    }
}

void rng_stop(void)
{
    RNG_Cmd(DISABLE);
    RCC_HBPeriphClockCmd(RCC_HBPeriph_RNG, DISABLE);
}
